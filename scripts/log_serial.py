"""ESP32 시리얼 로거 + 호흡 페이싱 (raw,mv 2채널).

아두이노가 한 줄에 'raw<TAB>mv' 를 보내는 경우(conductive_rubber_cord.ino)용.
CSV(data/breath_...csv)에 raw·mv·phase 를 기록하고, 끝나면 그래프를 띄운다.

실행:  python scripts/log_serial.py
설정은 아래 [설정] 블록만 고치면 된다. Ctrl+C 로 중지.
"""

import csv
import os
import sys
import time
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial 이 필요합니다.  pip install pyserial 로 설치하세요.")


# ============================================================
#  [설정]
# ============================================================
PORT = "/dev/cu.usbmodem5A671555851"   # 비우면("") 포트 목록만 보여주고 종료
BAUD = 115200
DURATION = 120.0     # 측정 시간(초)
INHALE = 1.5         # 흡기 안내 길이(초)
EXHALE = 2.5         # 호기 안내 길이(초)
BELL = False         # 전환 시 알림음
AUTO_PLOT = True     # 끝나면 자동으로 그래프
OUT_DIR = "data"     # CSV 저장 폴더
# ============================================================


def show_ports():
    ports = list(list_ports.comports())
    if not ports:
        print("연결된 시리얼 포트를 찾지 못했습니다.")
        return
    print("사용 가능한 포트:")
    for p in ports:
        print(f"  {p.device}   ({p.description})")


def phase_at(elapsed, inhale, exhale):
    cycle = inhale + exhale
    pos = elapsed % cycle
    index = int(elapsed // cycle) + 1
    if pos < inhale:
        return "inhale", pos, inhale, index
    return "exhale", pos - inhale, exhale, index


def parse_line(line):
    parts = line.split()
    if len(parts) != 2:
        return None
    try:
        return float(parts[0]), float(parts[1])
    except ValueError:
        return None


def status_line(elapsed, duration, phase, pos, length, index, raw, mv, rows):
    width = 12
    filled = min(width, int(width * pos / length)) if length > 0 else 0
    bar = "█" * filled + "·" * (width - filled)
    mark = "▲ 들이쉬기" if phase == "inhale" else "▼ 내쉬기 "
    remain = max(0.0, length - pos)
    total = f"{int(elapsed) // 60}:{int(elapsed) % 60:02d}"
    if duration:
        total += f" / {int(duration) // 60}:{int(duration) % 60:02d}"
    return (f"\r {total}  |  {mark} {bar} {remain:3.1f}s  "
            f"|  {index:3d}번째  |  raw {raw:>6g}  mv {mv:>6g}  ({rows}개)   ")


def countdown(seconds=3):
    print("\n자세를 편히 하고 스트랩이 잘 맞는지 확인하세요.\n")
    for i in range(seconds, 0, -1):
        print(f"\r  {i} ...   ", end="", flush=True)
        time.sleep(1.0)
    print("\r  시작!      \n")


def log(port, baud, out_path, duration, inhale, exhale, bell):
    print(f"포트 {port} 를 {baud} bps 로 엽니다...")
    try:
        ser = serial.Serial(port, baud, timeout=1)
    except serial.SerialException as e:
        print(f"\n포트를 열지 못했습니다: {e}\n")
        print("  - PORT 값과, Arduino IDE 시리얼 모니터가 닫혀 있는지 확인")
        show_ports()
        sys.exit(1)

    time.sleep(2.0)
    ser.reset_input_buffer()

    cycle = inhale + exhale
    print(f"호흡 안내: 흡기 {inhale}초 + 호기 {exhale}초 = 주기 {cycle}초 "
          f"(분당 약 {60.0/cycle:.1f}회)")
    countdown(3)
    print(f"기록 중 -> {out_path}   (Ctrl+C 로 중지)\n")

    rows = 0
    last_phase = None
    t0 = time.time()
    with open(out_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["elapsed_s", "timestamp", "raw", "mv", "phase", "cycle_index"])
        try:
            while True:
                elapsed = time.time() - t0
                if duration and elapsed >= duration:
                    print(f"\n\n지정한 {duration:.0f}초가 지나 종료합니다.")
                    break
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                if not line:
                    continue
                parsed = parse_line(line)
                if parsed is None:
                    continue
                raw, mv = parsed
                now = time.time()
                elapsed = now - t0
                phase, pos, length, index = phase_at(elapsed, inhale, exhale)
                writer.writerow([f"{elapsed:.3f}",
                                 datetime.fromtimestamp(now).strftime("%H:%M:%S.%f")[:-3],
                                 raw, mv, phase, index])
                rows += 1
                if bell and phase != last_phase:
                    print("\a", end="", flush=True)
                last_phase = phase
                print(status_line(elapsed, duration, phase, pos, length,
                                  index, raw, mv, rows), end="", flush=True)
        except KeyboardInterrupt:
            print("\n\n사용자가 중지했습니다.")
        finally:
            ser.close()
    print(f"저장 완료: {out_path}  (총 {rows}개)")
    return rows


def main():
    if not PORT:
        print("PORT 값을 설정하세요.\n")
        show_ports()
        return
    os.makedirs(OUT_DIR, exist_ok=True)
    out_path = os.path.join(OUT_DIR, f"breath_{datetime.now():%Y%m%d_%H%M%S}.csv")
    rows = log(PORT, BAUD, out_path, DURATION, INHALE, EXHALE, BELL)
    if rows and AUTO_PLOT:
        try:
            from plotting import plot_signals
            plot_signals(out_path)
        except ImportError:
            print("plotting 을 찾지 못해 자동 그래프를 건너뜁니다.")


if __name__ == "__main__":
    main()
