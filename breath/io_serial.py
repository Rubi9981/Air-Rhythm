"""시리얼 입력 — 포트 열기·줄 파싱·수신율 계량. 검출·플롯과 무관하다.

펌웨어(conductive_rubber_cord.ino)가 보내는 두 종류의 줄을 다룬다.

    "1116\t958"                              → 측정 샘플 (raw, mv)
    "# fs=50.00Hz avg=20000us min=... max=..." → 진단 (REPORT_RATE=true 일 때)

진단 줄을 읽어두면 **기기가 보고한 표본화 주파수**와 **호스트가 실제로 받은 수신율**을
나란히 볼 수 있다. 둘이 벌어지면 그 차이가 곧 유실된 샘플이다. 기기 타임스탬프 없이
얻을 수 있는 가장 직접적인 유실 감지다.

pyserial 은 이 모듈에서만 필요하므로 import 도 여기서만 한다.
"""

import re
import time
from collections import deque


def _serial():
    """pyserial 을 늦게 import 한다(설치 안 돼 있어도 다른 모듈은 쓸 수 있게)."""
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        raise SystemExit("pyserial 이 필요합니다.  pip install pyserial 로 설치하세요.")
    return serial, list_ports


def show_ports():
    """연결된 시리얼 포트를 출력한다."""
    _s, list_ports = _serial()
    ports = list(list_ports.comports())
    if not ports:
        print("연결된 시리얼 포트를 찾지 못했습니다.")
        return
    print("사용 가능한 포트:")
    for p in ports:
        print(f"  {p.device}   ({p.description})")


def open_port(port, baud, warmup_s=2.0, timeout=1.0):
    """포트를 열고 리셋 과도구간을 흘려보낸 뒤 돌려준다.

    ESP32 는 포트가 열릴 때 리셋되므로, 부팅 메시지가 지나갈 때까지 기다렸다가
    입력 버퍼를 비운다. 실패하면 원인과 포트 목록을 보여주고 종료한다.
    """
    serial, _lp = _serial()
    print(f"포트 {port} 를 {baud} bps 로 엽니다...")
    try:
        ser = serial.Serial(port, baud, timeout=timeout)
    except serial.SerialException as e:
        print(f"\n포트를 열지 못했습니다: {e}\n")
        print("  - PORT 값과, Arduino IDE 시리얼 모니터가 닫혀 있는지 확인")
        show_ports()
        raise SystemExit(1)
    time.sleep(warmup_s)
    ser.reset_input_buffer()
    return ser


def read_line(ser):
    """한 줄을 문자열로 읽는다. 타임아웃이면 빈 문자열."""
    return ser.readline().decode("utf-8", errors="ignore").strip()


def parse_sample(line):
    """'raw<TAB>mv' → (raw, mv). 형식이 아니면 None.

    필드가 정확히 2개일 때만 통과시키므로 '# fs=...' 같은 진단 줄은 걸러진다.
    """
    parts = line.split()
    if len(parts) != 2:
        return None
    try:
        return float(parts[0]), float(parts[1])
    except ValueError:
        return None


_DIAG_FS = re.compile(r"^#\s*fs=([0-9.]+)Hz")


def parse_diag(line):
    """'# fs=50.00Hz ...' → 50.0. 진단 줄이 아니면 None."""
    m = _DIAG_FS.match(line)
    return float(m.group(1)) if m else None


class RateMeter:
    """최근 window_s 초 동안 실제로 받은 샘플 수로 수신율(Hz)을 추정한다.

    창을 짧게 잡으면 표시가 요동친다 — USB CDC 가 여러 줄을 묶어 보내기 때문에
    순간 도착률은 들쭉날쭉하다. 3초면 150샘플이라 안정적이면서도 변화를 곧 보여준다.
    """

    def __init__(self, window_s=3.0):
        self.window_s = window_s
        self.stamps = deque()

    def tick(self, t):
        self.stamps.append(t)
        cutoff = t - self.window_s
        while self.stamps and self.stamps[0] < cutoff:
            self.stamps.popleft()

    def hz(self):
        """추정 수신율. 표본이 모자라면 None."""
        if len(self.stamps) < 2:
            return None
        span = self.stamps[-1] - self.stamps[0]
        return (len(self.stamps) - 1) / span if span > 0 else None
