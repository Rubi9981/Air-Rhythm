"""기울기(slope) 기반 흡기/호기 onset 검출 → 화살표 오버레이 그래프.

절댓값(진폭) 방식인 detect_breath.py 와 달리, 평활 기울기의 부호 전환으로
전환을 잡아 확정 지연을 줄인다(무호흡 판정은 없음).

읽기(breath.io_csv) → 필터(breath.filters) → 검출(SlopeDetector) → 그리기(plotting)
순서로 조립만 한다.

실행:
    python scripts/detect_slope.py data/breath_...csv
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from breath import config
from breath.io_csv import read_csv, target_spans, ask_csv_file, image_path_for
from breath.filters import bandpass_butter2, estimate_sample_rate
from breath.detectors import SlopeDetector, run_detector
from plotting import plot_detection


# ============================================================
#  [설정]
# ============================================================
HP, LP = config.HP_HZ, config.LP_HZ
ONSET_MARK = "confirm"        # "confirm"=확정 시점(제품용) / "extremum"=극점 소급
XLIM = None                   # 볼 구간(초). 전체면 None
DETECTOR_PARAMS = {}          # SlopeDetector 파라미터 오버라이드(예: {"k_slope":0.2})
# ============================================================


def run(path, save=None):
    if save is None:
        save = image_path_for(path, "_slope.png")

    times, _raws, mvs, phases = read_csv(path)
    fs = estimate_sample_rate(times)
    y_bp = bandpass_butter2(mvs, fs, HP, LP)

    det = SlopeDetector(**DETECTOR_PARAMS)
    inhale, exhale, apnea_spans, delays = run_detector(det, times, y_bp, mark=ONSET_MARK)
    bpm = det.bpm()
    mean_delay = sum(delays) / len(delays) if delays else 0.0

    print(f"파일     : {path}")
    print(f"샘플 수  : {len(times)}개  ({times[-1]-times[0]:.1f}초, 추정 {fs:.1f} Hz)")
    print(f"검출     : 기울기(slope) 기반  |  마커 위치: {ONSET_MARK}")
    print(f"흡기 시작: {len(inhale)}회   호기 시작: {len(exhale)}회")
    print(f"호흡률   : {bpm:.1f} bpm" if bpm else "호흡률   : (판정 불가)")
    print(f"검출 지연: 평균 {mean_delay*1000:.0f} ms  (극점 → 전환 확정까지)")

    spans = target_spans(times, phases, config.INHALE_S, config.EXHALE_S)
    title = f"{os.path.basename(path)}   |  slope-based detection"
    if bpm:
        title += f"  ~{bpm:.1f} bpm"
    plot_detection(times, mvs, y_bp, inhale, exhale, apnea_spans, spans,
                   bpm, HP, LP, title, save, xlim=XLIM)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else ask_csv_file()
    if not os.path.exists(path):
        sys.exit(f"파일을 찾을 수 없습니다: {path}")
    print()
    run(path)


if __name__ == "__main__":
    main()
