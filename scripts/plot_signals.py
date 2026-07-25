"""raw/mV 원본·스무딩·대역통과 다단 비교 그래프.

실행:
    python scripts/plot_signals.py data/breath_...csv
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from breath.io_csv import ask_csv_file
from plotting import plot_signals


# ============================================================
#  [설정]
# ============================================================
SMOOTH = 7            # Causal SMA 창 크기(0이면 스무딩선 없음)
XLIM = None           # 볼 구간(초). 전체면 None
BANDPASS = True       # 대역통과 패널 추가 여부
FULL_SCALE = False    # raw 축을 0~4095 로 고정
# ============================================================


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else ask_csv_file()
    if not os.path.exists(path):
        sys.exit(f"파일을 찾을 수 없습니다: {path}")
    plot_signals(path, smooth=SMOOTH, xlim=XLIM, bandpass=BANDPASS, full_scale=FULL_SCALE)


if __name__ == "__main__":
    main()
