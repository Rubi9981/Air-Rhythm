"""정확도 평가용 기준(정답) 라벨 — 오프라인·비인과적.

검출기는 미래 샘플을 볼 수 없지만, 기준 라벨은 녹음 전체를 보고 만든다.
  1. 양방향 필터: 같은 대역통과를 앞→뒤, 뒤→앞으로 한 번씩 → 위상 지연 0
  2. 국소 진폭: 주변 REF_AMP_WIN_S 창의 5~95 백분위 폭 (튀는 값 한두 개에 끌리지 않게)
  3. 극점: 마루/골에서 국소 진폭의 REF_DELTA_RATIO 만큼 되돌아가야 극점으로 인정
     (고전적인 peakdet). 마루와 골이 반드시 번갈아 나온다.
  4. 국소 진폭이 REF_MIN_AMP 보다 작으면 무호흡 구간('none')

이것은 **초안**이다. 같은 센서의 같은 신호로 만들었으므로 센서 자체의 오류(자세 변화로
생긴 가짜 파형 등)는 못 잡는다. scripts/label_reference.py 가 그려 주는 그림을 보고
라벨 파일을 손으로 고친 뒤 머리줄의 reviewed=no 를 yes 로 바꿀 것.

라벨 파일 (data/labels/<녹음 이름>.csv):
    # source=auto reviewed=no
    n,t_s,type
    612,12.24,inhale
    ...
  n     CSV 의 데이터 행 번호(0부터). 펌웨어 검출기의 샘플 번호와 같다
  t_s   그 행의 elapsed_s. 사람이 읽기 위한 것이고 평가는 n 만 쓴다
  type  이 샘플부터 다음 라벨까지의 상태
          inhale  흡기 시작(골)        exhale  호기 시작(마루)
          none    호흡 없음 — 이 구간의 검출은 전부 오검출
          skip    평가 제외 — 기침·자세 변경 등 정답을 정할 수 없는 구간
"""

import os

from . import config
from .detectors.slope import POLARITY
from .filters import bandpass_butter2


# --- 기준 라벨 조정값 ---
REF_AMP_WIN_S = 6.0      # 국소 진폭 창(초, 가운데 정렬)
REF_DELTA_RATIO = 0.4    # 극점 인정에 필요한 되돌림(국소 진폭 대비)
REF_MIN_AMP = 5.0        # 이보다 작으면 무호흡(mV). 검출기의 MIN_AMP 와 같은 값에서 시작

LABEL_TYPES = ("inhale", "exhale", "none", "skip")


def zero_phase_bandpass(values, fs, hp=config.HP_HZ, lp=config.LP_HZ):
    """대역통과를 앞뒤로 한 번씩 — 위상 지연이 없는 대신 인과적이지 않다."""
    forward = bandpass_butter2(values, fs, hp, lp)
    return bandpass_butter2(forward[::-1], fs, hp, lp)[::-1]


def local_amplitude(y, fs, win_s=REF_AMP_WIN_S):
    """각 샘플 주변 win_s 창의 5~95 백분위 폭. 0.5초마다 계산하고 사이는 유지한다."""
    n = len(y)
    half = max(1, int(win_s * fs / 2))
    step = max(1, int(fs / 2))
    amp = [0.0] * n
    for c in range(0, n, step):
        w = sorted(y[max(0, c - half):min(n, c + half)])
        a = w[int(0.95 * (len(w) - 1))] - w[int(0.05 * (len(w) - 1))]
        for i in range(c, min(n, c + step)):
            amp[i] = a
    return amp


def reference_labels(mvs, fs, polarity=POLARITY):
    """mV 신호 전체에서 기준 라벨 [(n, type), ...] 을 만든다(시간순)."""
    y = [polarity * v for v in zero_phase_bandpass(mvs, fs)]
    amp = local_amplitude(y, fs)

    labels = []
    state = None                  # None=모름 / 'max'=마루를 찾는 중 / 'min'=골을 찾는 중
    mx = mn = y[0]
    mx_n = mn_n = 0
    in_none = False
    for i, v in enumerate(y):
        if amp[i] < REF_MIN_AMP:
            if not in_none:
                labels.append((i, "none"))
                in_none = True
            state = None
            mx = mn = v
            mx_n = mn_n = i
            continue
        in_none = False
        delta = REF_DELTA_RATIO * amp[i]

        if v > mx:
            mx, mx_n = v, i
        if v < mn:
            mn, mn_n = v, i

        if state in (None, "max") and v < mx - delta:
            labels.append((mx_n, "exhale"))        # 마루 = 호기 시작
            state = "min"
            mn, mn_n = v, i
        elif state in (None, "min") and v > mn + delta:
            labels.append((mn_n, "inhale"))        # 골 = 흡기 시작
            state = "max"
            mx, mx_n = v, i

    # 극점은 확정된 뒤에 붙으므로 'none' 보다 뒤에 올 수 있다 — 샘플 순서로 정렬
    labels.sort(key=lambda x: x[0])
    return labels


def label_path_for(csv_path, label_dir=os.path.join("data", "labels")):
    base = os.path.splitext(os.path.basename(csv_path))[0]
    return os.path.join(label_dir, base + ".csv")


def write_labels(path, labels, times, source="auto"):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(f"# source={source} reviewed=no\n")
        f.write("n,t_s,type\n")
        for n, typ in labels:
            f.write(f"{n},{times[n]:.3f},{typ}\n")


def read_labels(path):
    """라벨 파일을 읽는다. 반환: (labels [(n, type)], reviewed bool)."""
    labels, reviewed = [], False
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                reviewed = reviewed or "reviewed=yes" in line
                continue
            if line.startswith("n,"):
                continue
            n, _t, typ = (s.strip() for s in line.split(","))
            if typ not in LABEL_TYPES:
                raise ValueError(f"{path}: 알 수 없는 라벨 '{typ}' ({line})")
            labels.append((int(n), typ))
    labels.sort(key=lambda x: x[0])
    return labels, reviewed
