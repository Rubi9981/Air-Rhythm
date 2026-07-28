"""실시간 흡기/호기 검출 모니터 — 시리얼을 읽어 터미널에 바로 보여준다.

log_serial.py 의 표시는 "사용자가 이때 들이쉬어야 한다"는 **페이싱 안내**라 센서를
뽑아도 똑같이 나온다. 이 스크립트는 반대로 **센서가 말하는 것**을 보여준다.

읽기(breath.io_serial) → 필터(BandpassButter2) → 검출(Detector) → 터미널
순서로 조립만 한다. 오프라인 경로(detect_slope.py)와 **같은 클래스**를 쓰므로
계산이 동일하고, 남긴 CSV 를 재생하면 같은 결론이 나와야 한다.

화면은 2단이다.
  - 흘러가는 줄: 전환이 확정된 순간(제품이 트리거될 시점)과 무신호/무호흡 구간.
    지워지면 안 되는 기록이다. CSV 의 event 열에도 같은 내용이 남는다.
  - 맨 아래 한 줄: 현재 상태. DRAW_HZ 로 제한해 덮어쓴다.

시작 전 READY_S 초의 준비 시간을 둔다. 그동안 도착하는 샘플은 읽어서 버린다
(그냥 기다리면 OS 버퍼에 쌓였다가 시작 직후 한꺼번에 쏟아진다).

실행:
    python scripts/monitor_breath.py        (Ctrl+C 로 중지)
"""

import csv
import os
import sys
import time
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from breath import config
from breath.filters import BandpassButter2
from breath.io_serial import (RateMeter, open_port, parse_diag, parse_sample,
                              read_line, show_ports)
from breath.detectors import AmplitudeDetector, SlopeDetector


# ============================================================
#  [설정]
# ============================================================
PORT = "/dev/cu.usbmodem5A671555851"   # 비우면("") 포트 목록만 보여주고 종료
BAUD = 115200
FS = 50.0              # 펌웨어 PERIOD_MS(20ms)와 일치시킬 것. Biquad 설계용
FS_TOL = 0.05          # 수신율이 이 비율 이상 어긋나면 '!' 표시
HP, LP = config.HP_HZ, config.LP_HZ
DETECTOR = "slope"     # "slope"(저지연) | "amplitude"(무호흡 판정 포함)
DETECTOR_PARAMS = {}   # 검출기 파라미터 오버라이드(예: {"min_phase_s": 0.6})
READY_S = 5.0          # 시작 전 준비 시간(초). 그동안 들어오는 샘플은 버린다
DRAW_HZ = 10.0         # 상태 줄 갱신 빈도. 50Hz로 그리면 터미널이 병목이 된다
BELL = False           # 전환 시 알림음
SAVE_CSV = True        # data/ 에 동시 기록(재생 대조용)
OUT_DIR = "data"

# 무신호(스트랩 풀림 등) 로그
NO_SIGNAL_MV = None    # 무신호 문턱(mV). None 이면 검출기의 min_amp, 없으면 5.0
NO_SIGNAL_HYST = 1.5   # 복귀는 문턱 × 이 배수를 넘어야 인정(문턱 근처 깜빡임 방지)
# ============================================================

DETECTORS = {"slope": SlopeDetector, "amplitude": AmplitudeDetector}
CSV_HEADER = ["elapsed_s", "timestamp", "raw", "mv",
              "phase", "cycle_index", "det_phase", "event"]

INHALE_MARK = "▲ 흡기"
EXHALE_MARK = "▼ 호기"


class Screen:
    """흐르는 이벤트 줄과 제자리 갱신되는 상태 줄을 함께 쓰기 위한 최소 도우미.

    '\\r' 로 덮어쓰는 상태 줄이 떠 있는 채로 '\\n' 을 찍으면 화면이 깨지므로,
    이벤트를 출력하기 전에 상태 줄을 지운다.
    """

    def __init__(self):
        self.width = 0

    def event(self, text):
        if self.width:
            print("\r" + " " * self.width + "\r", end="")
            self.width = 0
        print(text, flush=True)

    def status(self, text):
        pad = max(0, self.width - len(text))
        print("\r" + text + " " * pad, end="", flush=True)
        self.width = len(text)

    def close(self):
        if self.width:
            print()
            self.width = 0


def phase_label(det, elapsed):
    """현재 위상을 사람이 읽는 문자열로. 정착 중과 무신호를 구분한다.

    정착 잔여는 검출기 자신의 기준 시각(det.t0)으로 재야 스크립트 시작과
    첫 샘플 도착 사이의 차이만큼 어긋나지 않는다.
    """
    settled = det.t0 if det.t0 is not None else 0.0
    remain = det.settle_s - (elapsed - settled)
    if remain > 0:
        return f"정착 중  {remain:4.1f}초 남음"
    # 무호흡 중에는 전환이 막히므로 위상이 직전 값에 머문다. 먼저 확인한다.
    if getattr(det, "in_apnea", False):
        return "무호흡"
    if isinstance(det, SlopeDetector):
        if det.phase == SlopeDetector.RISING:
            return INHALE_MARK
        if det.phase == SlopeDetector.FALLING:
            return EXHALE_MARK
    else:
        if det.phase == AmplitudeDetector.INHALE:
            return INHALE_MARK
        if det.phase == AmplitudeDetector.EXHALE:
            return EXHALE_MARK
    return f"신호 없음 ({det.amplitude():.0f}mV)"


def rate_label(dev_fs, host_hz):
    """기기 보고 주파수 / 호스트 수신율. 어긋나면 '!'."""
    dev = f"{dev_fs:.1f}" if dev_fs else " -- "
    if host_hz is None:
        return f"{dev}/ -- Hz "
    flag = "!" if abs(host_hz - FS) / FS > FS_TOL else " "
    return f"{dev}/{host_hz:.1f}Hz{flag}"


def mmss(t):
    return f"{int(t) // 60}:{int(t) % 60:02d}"


def event_line(ev, det):
    """전환/무호흡/무신호 이벤트 한 줄. 지연은 극점 → 확정까지 걸린 시간."""
    kind = ev["type"]
    stamp = f"  {mmss(ev['t'])}.{int(ev['t'] % 1 * 100):02d}"
    if kind in ("inhale_onset", "exhale_onset"):
        mark = INHALE_MARK if kind == "inhale_onset" else EXHALE_MARK
        delay_ms = (ev["t"] - ev["ext_t"]) * 1000.0
        bpm = det.bpm()
        rate = f"   {bpm:4.1f} bpm" if bpm else ""
        return f"{stamp}   {mark} 시작   (지연 {delay_ms:4.0f}ms){rate}"
    if kind == "signal_lost":
        return f"{stamp}   ○ 신호 없음   (진폭 {ev['y']:.1f}mV — 스트랩 확인)"
    if kind == "signal_ok":
        return f"{stamp}   ● 신호 복귀   (진폭 {ev['y']:.1f}mV)"
    label = "무호흡 시작" if kind == "apnea_start" else "무호흡 종료"
    return f"{stamp}   {label}"


def check_signal(det, elapsed, signal_ok, threshold):
    """무신호 상태가 바뀌었으면 이벤트를, 아니면 None 을 돌려준다.

    검출기는 무신호 이벤트를 내지 않는다(SlopeDetector 는 조용히 판정을 보류하고,
    AmplitudeDetector 는 아예 그런 개념이 없다). 그래서 진폭을 직접 보고 판정한다.
    복귀 문턱을 조금 높여(NO_SIGNAL_HYST) 문턱 근처에서 깜빡이는 것을 막는다.
    """
    amp = det.amplitude()
    if signal_ok and amp < threshold:
        return {"type": "signal_lost", "t": elapsed, "y": amp}
    if not signal_ok and amp >= threshold * NO_SIGNAL_HYST:
        return {"type": "signal_ok", "t": elapsed, "y": amp}
    return None


def countdown(seconds, ser):
    """시작 전 준비 시간. 그동안 도착하는 샘플은 읽어서 버린다.

    그냥 sleep 하면 시리얼 데이터가 OS 버퍼에 쌓였다가 시작 직후 한꺼번에
    쏟아진다. 그러면 앞부분 샘플의 타임스탬프가 뭉쳐 dt 가 0에 가까워지므로
    반드시 흘려보내야 한다.
    """
    print("자세를 편히 하고 스트랩이 잘 맞는지 확인하세요.\n")
    end = time.time() + seconds
    last_draw = 0.0
    while True:
        remain = end - time.time()
        if remain <= 0:
            break
        read_line(ser)                      # 버림
        now = time.time()
        if now - last_draw >= 1.0 / DRAW_HZ:
            print(f"\r  준비  {remain:4.1f}초 ...   ", end="", flush=True)
            last_draw = now
    print("\r  시작!                \n")
    ser.reset_input_buffer()


def status_line(elapsed, det, dev_fs, host_hz, rows):
    bpm = det.bpm()
    bpm_txt = f"{bpm:5.1f} bpm" if bpm else "  --  bpm"
    return (f" {mmss(elapsed):>5}  |  {phase_label(det, elapsed):<22} |  {bpm_txt}"
            f"  |  진폭 {det.amplitude():5.1f}mV  |  {rate_label(dev_fs, host_hz)}"
            f"  |  {rows}개")


def open_csv(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"breath_{datetime.now():%Y%m%d_%H%M%S}.csv")
    f = open(path, "w", newline="", encoding="utf-8")
    writer = csv.writer(f)
    writer.writerow(CSV_HEADER)
    return path, f, writer


def det_phase_name(det):
    if isinstance(det, SlopeDetector):
        return {SlopeDetector.RISING: "inhale",
                SlopeDetector.FALLING: "exhale"}.get(det.phase, "unknown")
    return det.phase


def run():
    det = DETECTORS[DETECTOR](**DETECTOR_PARAMS)
    bp = BandpassButter2(FS, HP, LP)
    rate = RateMeter(3.0)
    screen = Screen()

    no_signal_mv = NO_SIGNAL_MV if NO_SIGNAL_MV is not None else getattr(det, "min_amp", 5.0)

    ser = open_port(PORT, BAUD)

    print(f"검출     : {DETECTOR}  |  대역통과 {HP}~{LP}Hz  |  설계 fs {FS:.1f}Hz")
    print(f"무신호   : 진폭 {no_signal_mv:.1f}mV 미만")
    print(f"정착     : 시작 후 {det.settle_s:.0f}초 뒤부터 판정합니다.  (Ctrl+C 로 중지)\n")

    countdown(READY_S, ser)

    path, fh, writer = (None, None, None)
    if SAVE_CSV:
        path, fh, writer = open_csv(OUT_DIR)
        print(f"기록 -> {path}\n")

    t0 = time.time()
    rows = 0
    dev_fs = None
    last_draw = 0.0
    signal_ok = True
    try:
        while True:
            line = read_line(ser)
            if not line:
                continue

            fs_reported = parse_diag(line)
            if fs_reported is not None:      # 펌웨어 진단 줄
                dev_fs = fs_reported
                continue

            parsed = parse_sample(line)
            if parsed is None:
                continue
            raw, mv = parsed

            now = time.time()
            elapsed = now - t0
            y = bp.update(mv)                # 오프라인과 동일한 필터
            ev = det.update(elapsed, y)      # 오프라인과 동일한 검출기
            rate.tick(elapsed)
            rows += 1

            events = [ev] if ev else []
            # 무신호는 정착이 끝난 뒤부터만 판정한다(그 전에는 진폭이 아직 자라는 중)
            if det.t0 is not None and elapsed - det.t0 >= det.settle_s:
                sig = check_signal(det, elapsed, signal_ok, no_signal_mv)
                if sig:
                    signal_ok = (sig["type"] == "signal_ok")
                    events.append(sig)

            if writer:
                writer.writerow([f"{elapsed:.3f}",
                                 datetime.fromtimestamp(now).strftime("%H:%M:%S.%f")[:-3],
                                 raw, mv, "", "",
                                 det_phase_name(det),
                                 ";".join(e["type"] for e in events)])

            for e in events:
                screen.event(event_line(e, det))
                if BELL and e["type"].endswith("_onset"):
                    print("\a", end="", flush=True)

            if now - last_draw >= 1.0 / DRAW_HZ:
                screen.status(status_line(elapsed, det, dev_fs, rate.hz(), rows))
                last_draw = now
    except KeyboardInterrupt:
        screen.close()
        print("\n중지했습니다.")
    finally:
        screen.close()
        ser.close()
        if fh:
            fh.close()

    bpm = det.bpm()
    print(f"\n총 {rows}개 샘플, {time.time() - t0:.1f}초")
    print(f"호흡률   : {bpm:.1f} bpm" if bpm else "호흡률   : (판정 불가)")
    if path:
        print(f"저장 완료: {path}")
        print(f"재생 대조: python scripts/detect_{'slope' if DETECTOR == 'slope' else 'breath'}.py {path}")


def main():
    if not PORT:
        print("PORT 값을 설정하세요.\n")
        show_ports()
        return
    if DETECTOR not in DETECTORS:
        sys.exit(f"DETECTOR 는 {list(DETECTORS)} 중 하나여야 합니다: {DETECTOR!r}")
    print()
    run()


if __name__ == "__main__":
    main()
