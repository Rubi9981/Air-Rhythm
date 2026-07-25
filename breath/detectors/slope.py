"""기울기(slope) 기반 호기/흡기 검출기 — 저지연.

진폭 히스테리시스(AmplitudeDetector)는 극점이 평평해 확정이 늦다(~150ms 바닥).
이 검출기는 **평활된 기울기의 부호 전환**으로 트리거한다. 극점 직후 기울기는
0을 빠르게 지나므로 작은 문턱을 금방 넘어 확정이 극점에 훨씬 가깝다.

정확도 장치(오검출 억제):
  - 적응형 기울기 데드밴드(Schmitt): sth = max(K_SLOPE·평균|기울기|, SLOPE_FLOOR)
  - 최소 위상시간 잠금(MIN_PHASE_S): double-hump 억제의 주력. 지연을 늘리지 않는다.
  - 진폭 하한(MIN_AMP): 무신호 구간 판정 보류
  - (옵션) prominence 게이트(PROM_RATIO>0): 골/마루에서 그만큼 되돌아온 뒤 확정.
    노이즈에 더 강해지지만 그 상승/하강을 기다리므로 지연이 늘어난다. 기본은 0(off).

무호흡 판정은 포함하지 않는다. 이벤트 스키마는 AmplitudeDetector 와 동일하므로
run_detector·plot_detection 을 그대로 쓸 수 있다. 미래 샘플을 참조하지 않는다.

튜닝 요지: 지연은 SLOPE_TAU_S 가, 정확도(double-hump)는 MIN_PHASE_S 가 지배한다.
MIN_PHASE_S 는 "가장 짧은 반주기" 보다 작아야 한다(기본 1.2s → 최대 ~25bpm까지 안전).
"""

from statistics import median

from .base import Detector


# --- 기울기 트리거 (지연을 지배) ---
SLOPE_TAU_S = 0.08    # 기울기 평활 시상수(초). 작을수록 빠르지만 노이즈에 약함
AVG_TAU_S = 8.0       # 평균|기울기| 추정 시상수(적응형 데드밴드용)
K_SLOPE = 0.25        # 데드밴드 = K_SLOPE × 평균|기울기|
SLOPE_FLOOR = 1.0     # 데드밴드 절대 하한(mV/s). -1mV~1mV 이면 전환 안함.

# --- 정확도 장치 ---
MIN_PHASE_S = 1.2     # 전환 직후 반대 전환 금지(초). double-hump 억제 주력
PROM_RATIO = 0.0      # 골/마루에서 되돌림 요구(진폭 대비). >0이면 지연↑. 기본 off
MIN_AMP = 5.0         # 최근 진폭(p-p)이 이보다 작으면 판정 보류(mV)
ENV_DECAY_S = 6.0     # 진폭 포락선 완화 시상수(초)

SETTLE_S = 10.0       # 시작 과도응답 구간(판정 보류)
POLARITY = +1         # +1: 상승=흡기. 센서 반대로 붙였으면 -1

# --- 호흡률 ---
RATE_WINDOW = 5
RATE_MIN_S = 1.4
RATE_MAX_S = 12.0


class SlopeDetector(Detector):
    """평활 기울기의 부호 전환으로 흡기/호기 onset 을 잡는 인과적 검출기."""

    RISING = +1     # 상승 중 = 흡기
    FALLING = -1    # 하강 중 = 호기
    UNKNOWN = 0

    def __init__(self, slope_tau_s=SLOPE_TAU_S, avg_tau_s=AVG_TAU_S,
                 k_slope=K_SLOPE, slope_floor=SLOPE_FLOOR,
                 min_phase_s=MIN_PHASE_S, prom_ratio=PROM_RATIO, min_amp=MIN_AMP,
                 env_decay_s=ENV_DECAY_S, settle_s=SETTLE_S, polarity=POLARITY,
                 rate_window=RATE_WINDOW, rate_min_s=RATE_MIN_S, rate_max_s=RATE_MAX_S):
        self.slope_tau_s = slope_tau_s
        self.avg_tau_s = avg_tau_s
        self.k_slope = k_slope
        self.slope_floor = slope_floor
        self.min_phase_s = min_phase_s
        self.prom_ratio = prom_ratio
        self.min_amp = min_amp
        self.env_decay_s = env_decay_s
        self.settle_s = settle_s
        self.polarity = polarity
        self.rate_window = rate_window
        self.rate_min_s = rate_min_s
        self.rate_max_s = rate_max_s

        self.t0 = None
        self.last_t = None
        self.y_prev = None
        self.sd = 0.0            # 평활 기울기
        self.avg_abs_sd = 0.0    # 평균|기울기|
        self.env_hi = None
        self.env_lo = None

        self.phase = self.UNKNOWN
        self.ext_val = None      # 현재 구간의 극값(polarity 적용값) — FALLING이면 골, RISING이면 마루
        self.ext_t = None        # 극값 시각
        self.ext_yraw = None     # 극값에서의 원(대역통과) 값 — 마커/보고용
        self.last_transition_t = None
        self.inhale_onsets = []

    def _amplitude(self):
        if self.env_hi is None or self.env_lo is None:
            return 0.0
        return self.env_hi - self.env_lo

    def _seed_phase(self, y, t, y_raw):
        self.ext_val = y
        self.ext_t = t
        self.ext_yraw = y_raw

    def update(self, t, y_raw):
        y = self.polarity * y_raw
        if self.t0 is None:
            self.t0 = self.last_t = t
            self.y_prev = y
            self.env_hi = self.env_lo = y
            self._seed_phase(y, t, y_raw)
            self.last_transition_t = t
            return None

        dt = t - self.last_t
        self.last_t = t
        if dt <= 0:
            dt = 1e-6

        # 기울기(1차 차분) → 평활 → 평균|기울기|
        d = (y - self.y_prev) / dt
        self.y_prev = y
        a_slope = dt / (self.slope_tau_s + dt)
        self.sd += a_slope * (d - self.sd)
        a_avg = dt / (self.avg_tau_s + dt)
        self.avg_abs_sd += a_avg * (abs(self.sd) - self.avg_abs_sd)

        # 진폭 포락선
        amp0 = self._amplitude()
        decay = (amp0 if amp0 > 0 else 1.0) * dt / max(self.env_decay_s, 1e-6)
        self.env_hi = max(y, self.env_hi - decay)
        self.env_lo = min(y, self.env_lo + decay)
        amp = self._amplitude()

        sth = max(self.k_slope * self.avg_abs_sd, self.slope_floor)

        # 정착 전 / 무신호: 판정 보류(극값은 최신으로 유지)
        if t - self.t0 < self.settle_s or amp < self.min_amp:
            self._seed_phase(y, t, y_raw)
            self.phase = self.UNKNOWN
            return None

        can_switch = (t - self.last_transition_t) >= self.min_phase_s

        if self.phase == self.UNKNOWN:
            if self.sd > sth:
                self.phase = self.RISING
                self._seed_phase(y, t, y_raw)
                self.last_transition_t = t
            elif self.sd < -sth:
                self.phase = self.FALLING
                self._seed_phase(y, t, y_raw)
                self.last_transition_t = t
            return None

        if self.phase == self.FALLING:
            if y < self.ext_val:                       # 골 추적
                self.ext_val, self.ext_t, self.ext_yraw = y, t, y_raw
            # 골에서 prom·amp 만큼 되돌아 올라왔는가 (교착 없음)
            if can_switch and self.sd > sth and (y - self.ext_val) >= self.prom_ratio * amp:
                ev = {"type": "inhale_onset", "t": t, "y": y_raw,
                      "ext_t": self.ext_t, "ext_y": self.ext_yraw}
                self.inhale_onsets.append(t)
                self.phase = self.RISING
                self.last_transition_t = t
                self.ext_val, self.ext_t, self.ext_yraw = y, t, y_raw  # 새 구간 극값 시드
                return ev
        else:  # RISING
            if y > self.ext_val:                       # 마루 추적
                self.ext_val, self.ext_t, self.ext_yraw = y, t, y_raw
            # 마루에서 prom·amp 만큼 되돌아 내려왔는가
            if can_switch and self.sd < -sth and (self.ext_val - y) >= self.prom_ratio * amp:
                ev = {"type": "exhale_onset", "t": t, "y": y_raw,
                      "ext_t": self.ext_t, "ext_y": self.ext_yraw}
                self.phase = self.FALLING
                self.last_transition_t = t
                self.ext_val, self.ext_t, self.ext_yraw = y, t, y_raw
                return ev

        return None

    def bpm(self):
        """최근 흡기 간격 중앙값으로 분당 호흡수. 없으면 None."""
        if len(self.inhale_onsets) < 2:
            return None
        intervals = [b - a for a, b in zip(self.inhale_onsets, self.inhale_onsets[1:])]
        intervals = [d for d in intervals if self.rate_min_s <= d <= self.rate_max_s]
        if not intervals:
            return None
        return 60.0 / median(intervals[-self.rate_window:])
