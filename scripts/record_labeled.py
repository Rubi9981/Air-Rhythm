"""키보드 정답 라벨과 함께 기록 — 숨을 들이쉬기 시작할 때 i, 내쉬기 시작할 때 e.

기기(firmware/prototype)가 보내는 샘플 줄을 CSV 로 저장하면서, 사람이 누른 키를
"그 순간 가장 최근에 받은 샘플" 에 붙인다. 기기가 보낸 이벤트 줄(# INHALE / # EXHALE / ...)도
같은 샘플 행에 함께 남긴다. 기기는 샘플 줄을 먼저, 그 샘플의 이벤트 줄을 바로 뒤에 찍는다.

준비:
  - board_config.h 의 REPORT_SAMPLE 을 true 로 바꿔 올린다. 아니면 샘플 줄이 오지 않는다
  - 대기 화면(모드 선택)에서 기록한다. 센서는 어느 화면에서든 돌고 모터는 호흡 실행 화면에서만 돈다
  - 입력기를 영문으로 둔다. 한글 입력 상태에서는 키가 조합 중에 묶여 늦게 올 수 있다

누르는 법:
  - 공기가 들어오기 시작하는 순간 i, 나가기 시작하는 순간 e. 본 기록 전에 1분쯤 연습할 것
  - 페이싱 안내는 보지 않는다(안내에 맞춰 누르면 라벨이 호흡이 아니라 안내가 된다)
  - 키보드는 몸에서 떨어진 책상에, 팔은 고정. 팔 움직임이 흉부 센서에 실린다
  - 같은 키를 두 번 누르면 두 번째는 무시한다. q 로 끝낸다(Ctrl+C 도 된다)

CSV 열 (앞 네 열의 순서는 바꾸지 않는다 — host_test 의 csv.h 가 위치로 읽는다):
    elapsed_s, timestamp, raw, mv, phase, cycle_index, key, fw_event
  phase, cycle_index  비어 있다(페이싱 없음). 기존 CSV 와 열을 맞추려고 둔다
  key                 그 샘플의 키보드 상태 inhale / exhale (첫 키 전에는 빈칸)
  fw_event            그 샘플에 기기가 보낸 # 줄 (여럿이면 ';')

키 반응 지연(200~400ms)이 섞이므로 키 라벨은 횟수·놓침·오검출을 보는 데 쓰고
지연 측정에는 쓰지 않는다. 비교는 scripts/nk_check.py 가 한다.

실행:
    python scripts/record_labeled.py --id S01
    python scripts/record_labeled.py --id S01 --duration 180
"""

import argparse
import csv
import os
import re
import sys
import time
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from breath.io_serial import RateMeter, open_port, parse_sample, show_ports


# ============================================================
#  [설정]
# ============================================================
PORT = "/dev/cu.usbmodem5A671555851"   # 비우면("") 포트 목록만 보여주고 종료
BAUD = 115200
DURATION = 0.0       # 기록 시간(초). 0 이면 q 를 누를 때까지
READY_S = 3.0        # 시작 전 준비 시간. 그동안 들어오는 샘플은 버린다(버퍼에 쌓인 것)
NO_SAMPLE_S = 5.0    # 이 시간 동안 샘플 줄이 하나도 없으면 REPORT_SAMPLE 을 의심하고 끝낸다
DRAW_HZ = 10.0       # 상태 줄 갱신 빈도
OUT_DIR = "data"
# 한글 입력 상태에서 같은 자리를 누르면 오는 글자도 받아 준다(ㅑ=i, ㄷ=e, ㅂ=q)
KEYS_INHALE = ("i", "I", "ㅑ")
KEYS_EXHALE = ("e", "E", "ㄷ")
KEYS_QUIT = ("q", "Q", "ㅂ")
# ============================================================

CSV_HEADER = ["elapsed_s", "timestamp", "raw", "mv", "phase", "cycle_index", "key", "fw_event"]
_EVENT_N = re.compile(r"^#\s*(INHALE|EXHALE|NOSIG|SIGOK)\s+n=(\d+)")


class KeyReader:
    """터미널에서 키를 하나씩, 기다리지 않고 읽는다(엔터 불필요)."""

    def __enter__(self):
        if os.name == "nt":
            import msvcrt
            self._msvcrt = msvcrt
        else:
            import termios
            import tty
            self._fd = sys.stdin.fileno()
            self._old = termios.tcgetattr(self._fd)
            tty.setcbreak(self._fd)          # 줄 단위 입력·에코 끔. Ctrl+C 는 그대로 동작
        return self

    def __exit__(self, *exc):
        if os.name != "nt":
            import termios
            termios.tcsetattr(self._fd, termios.TCSADRAIN, self._old)

    def read(self):
        """지금까지 눌린 글자들. 없으면 빈 문자열."""
        if os.name == "nt":
            out = ""
            while self._msvcrt.kbhit():
                out += self._msvcrt.getwch()
            return out
        import select
        if not select.select([sys.stdin], [], [], 0)[0]:
            return ""
        return os.read(self._fd, 64).decode("utf-8", errors="ignore")


class Recorder:
    """샘플 행을 하나 늦게 쓴다 — 뒤따라오는 이벤트 줄과 키를 그 행에 붙이기 위해서."""

    def __init__(self, writer):
        self.writer = writer
        self.pending = None
        self.rows = 0                 # 쓰기 끝난 행 수 = 다음 행 번호
        self.key = ""                 # 현재 키보드 상태
        self.key_onsets = {"inhale": 0, "exhale": 0}
        self.fw_onsets = {"inhale": 0, "exhale": 0}
        self.offsets = [[]]           # 기기 이벤트 n - 행 번호. RESET 마다 새 묶음
        self.qdrops = 0
        self.last_fw = ""

    @property
    def index(self):
        """pending 행의 번호(= 가장 최근에 받은 샘플). 아직 없으면 None."""
        return self.rows if self.pending is not None else None

    def sample(self, elapsed, now, raw, mv):
        self.flush()
        self.pending = [f"{elapsed:.3f}", datetime.fromtimestamp(now).strftime("%H:%M:%S.%f")[:-3],
                        f"{raw:g}", f"{mv:g}", "", "", self.key, ""]

    def device_line(self, line):
        """기기의 # 줄. 이벤트면 직전 샘플 행에 붙이고 유실 검사용 n 을 모은다."""
        if line.startswith("# QDROP"):
            self.qdrops += 1
        if line.startswith("# RESET"):
            self.offsets.append([])
        m = _EVENT_N.match(line)
        if m and self.pending is not None:
            kind, n = m.group(1), int(m.group(2))
            self.offsets[-1].append(n - self.index)
            if kind in ("INHALE", "EXHALE"):
                self.fw_onsets[kind.lower()] += 1
            self.last_fw = kind
        if self.pending is not None and (m or line.startswith(("# RESET", "# SETTLED", "# QDROP"))):
            text = line[1:].strip()
            self.pending[7] = f"{self.pending[7]};{text}" if self.pending[7] else text

    def press(self, kind):
        """키 입력. 가장 최근 샘플부터 새 상태로 바꾼다. 같은 상태면 무시(False)."""
        if self.pending is None or kind == self.key:
            return False
        self.key = kind
        self.pending[6] = kind
        self.key_onsets[kind] += 1
        return True

    def flush(self):
        if self.pending is not None:
            self.writer.writerow(self.pending)
            self.rows += 1
            self.pending = None


def status(elapsed, duration, rec, hz):
    total = f"{int(elapsed) // 60}:{int(elapsed) % 60:02d}"
    if duration:
        total += f" / {int(duration) // 60}:{int(duration) % 60:02d}"
    mark = {"inhale": "▲ 흡기", "exhale": "▼ 호기"}.get(rec.key, "- 대기")
    rate = f"{hz:4.1f}Hz" if hz else "  -  "
    return (f"\r {total}  |  키 {mark}  (i {rec.key_onsets['inhale']:3d} / e {rec.key_onsets['exhale']:3d})"
            f"  |  기기 흡기 {rec.fw_onsets['inhale']:3d} 호기 {rec.fw_onsets['exhale']:3d}"
            f"  |  {rec.rows + (rec.pending is not None):6d}샘플 {rate}   ")


def record(port, baud, out_path, duration):
    ser = open_port(port, baud, timeout=0)
    print(f"\n준비 {READY_S:.0f}초 — 영문 입력 상태인지 확인하세요. i = 들이쉬기 시작, "
          f"e = 내쉬기 시작, q = 끝")
    t_end = time.time() + READY_S
    while time.time() < t_end:                    # 버퍼에 쌓인 것 버리기
        ser.read(ser.in_waiting or 1)
        time.sleep(0.05)
    ser.reset_input_buffer()
    print(f"기록 중 -> {out_path}\n")

    rate = RateMeter()
    buf = b""
    t0 = None
    t_start = time.time()
    last_draw = 0.0
    with open(out_path, "w", newline="", encoding="utf-8") as f, KeyReader() as keys:
        writer = csv.writer(f)
        writer.writerow(CSV_HEADER)
        rec = Recorder(writer)
        try:
            while True:
                now = time.time()
                if duration and t0 is not None and now - t0 >= duration:
                    print(f"\n\n지정한 {duration:.0f}초가 지나 종료합니다.")
                    break
                if t0 is None and now - t_start > NO_SAMPLE_S:
                    print(f"\n\n{NO_SAMPLE_S:.0f}초 동안 샘플 줄이 없습니다. board_config.h 의 "
                          f"REPORT_SAMPLE 이 true 인지, PORT 가 맞는지 확인하세요.")
                    break

                # 키를 먼저 본다 — 그래야 "지금까지 받은 가장 최근 샘플" 에 붙는다
                typed = keys.read()
                if any(c in KEYS_QUIT for c in typed):
                    print("\n\nq — 종료합니다.")
                    break
                for c in typed:
                    if c in KEYS_INHALE:
                        rec.press("inhale")
                    elif c in KEYS_EXHALE:
                        rec.press("exhale")

                chunk = ser.read(ser.in_waiting or 1)
                if not chunk:
                    time.sleep(0.002)
                    continue
                buf += chunk
                *lines, buf = buf.split(b"\n")
                for raw_line in lines:
                    line = raw_line.decode("utf-8", errors="ignore").strip()
                    if not line:
                        continue
                    if line.startswith("#"):
                        rec.device_line(line)
                        continue
                    if line[0] in "|+":                  # LCD 화면 테두리 줄
                        continue
                    parsed = parse_sample(line)
                    if parsed is None:
                        continue
                    t = time.time()
                    if t0 is None:
                        t0 = t
                    rate.tick(t)
                    rec.sample(t - t0, t, *parsed)

                if now - last_draw >= 1.0 / DRAW_HZ and t0 is not None:
                    last_draw = now
                    print(status(now - t0, duration, rec, rate.hz()), end="", flush=True)
        except KeyboardInterrupt:
            print("\n\n사용자가 중지했습니다.")
        finally:
            rec.flush()
            ser.close()
    return rec


def report(rec, out_path):
    print(f"\n저장: {out_path}  ({rec.rows}샘플, 약 {rec.rows / 50:.0f}초)")
    print(f"  키    흡기 {rec.key_onsets['inhale']}회 / 호기 {rec.key_onsets['exhale']}회")
    print(f"  기기  흡기 {rec.fw_onsets['inhale']}회 / 호기 {rec.fw_onsets['exhale']}회")
    # 기기 이벤트의 n 은 기기가 센 샘플 번호다. 샘플이 하나도 안 빠졌다면 (n - 행 번호) 가
    # 회차 안에서 일정해야 한다. 달라지면 그 사이에 샘플 줄이 빠진 것이다.
    groups = [g for g in rec.offsets if g]
    if not groups:
        print("  유실  확인 못 함 (기기 이벤트가 없었음)")
    elif all(min(g) == max(g) for g in groups):
        print("  유실  없음 (기기 이벤트 번호와 행 번호가 끝까지 맞음)")
    else:
        lost = sum(max(g) - min(g) for g in groups)
        print(f"  유실  **약 {lost}샘플 빠짐** — 이 파일로 잰 시각은 그만큼 어긋난다")
    if rec.qdrops:
        print(f"  QDROP {rec.qdrops}회 — 기기 안에서 앱 태스크가 밀렸다")
    print(f"\n비교:  python scripts/nk_check.py {out_path} -v --plot")


def main():
    ap = argparse.ArgumentParser(description="키보드 정답 라벨과 함께 기록 (i=흡기, e=호기)")
    ap.add_argument("--id", default="rec", help="파일 이름 앞부분. 실명 대신 피험자 ID (예: S01)")
    ap.add_argument("--port", default=PORT)
    ap.add_argument("--duration", type=float, default=DURATION, help="기록 시간(초). 0 이면 q 까지")
    args = ap.parse_args()

    if not args.port:
        print("PORT 값을 설정하세요.\n")
        show_ports()
        return
    if not sys.stdin.isatty():
        sys.exit("키 입력을 받으려면 터미널에서 직접 실행해야 합니다.")
    os.makedirs(OUT_DIR, exist_ok=True)
    out_path = os.path.join(OUT_DIR, f"{args.id}_{datetime.now():%Y%m%d_%H%M%S}.csv")
    rec = record(args.port, BAUD, out_path, args.duration)
    report(rec, out_path)


if __name__ == "__main__":
    main()
