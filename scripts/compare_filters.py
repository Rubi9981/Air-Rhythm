"""mV 신호에 1-pole vs 2차 Butterworth 대역통과를 비교하는 3단 그래프.

실행:
    python scripts/compare_filters.py data/breath_...csv
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from breath.io_csv import ask_csv_file
from plotting import plot_filter_compare


# ============================================================
#  [설정]
# ============================================================
XLIM = None           # 볼 구간(초). 전체면 None
# ============================================================


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else ask_csv_file()
    if not os.path.exists(path):
        sys.exit(f"파일을 찾을 수 없습니다: {path}")
    plot_filter_compare(path, xlim=XLIM)


if __name__ == "__main__":
    main()
