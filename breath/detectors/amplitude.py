"""진폭 히스테리시스 검출기 — 골/마루에서 적응형 문턱만큼 되돌아오면 전환.

논문(Sang et al. 2024)의 기울기 방향 + 특허(US11,324,950)의 적응형 기준
(midpoint=0.5 peak-to-peak, moving baseline)을 결합한 인과적 상태기계.

한계: 극점 근처에서 신호가 평평해 확정이 늦다(문턱 √법칙). 100ms 이하가
필요하면 기울기 0교차 검출기(향후 slope.py)를 쓴다.
"""

from statistics import median

from .base import Detector


# --- 전환 문턱 ---
K_DELTA = 0.1         # 호흡 주기 전환에 필요한 값 = K_DELTA × 최근 peak-to-peak 진폭
DELTA_FLOOR = 2.0     # 호흡 주기 전환 시 필요한 최소 mV 값
MIN_PHASE_S = 0.5     # 전환 직후 반대 전환 금지 시간
SETTLE_S = 10.0       # 센서 안정화 시간 (10초)
ENV_DECAY_S = 6.0     # 진폭 포락선 완화 시상수(초), peak-to-peak와 무호흡 검출에 사용
POLARITY = +1         # +1: 상승=흡기. 센서 반대로 붙였으면 -1

# --- 호흡률 ---
RATE_WINDOW = 5       # 최근 흡기 간격 몇 개의 중앙값
RATE_MIN_S = 1.4
RATE_MAX_S = 12.0

# --- 무호흡 ---
APNEA_FRAC = 0.30     # 진폭이 기준선의 이 비율 아래면 무호흡 후보
APNEA_MIN_S = 8.0     # 지속 시간 임계
BASELINE_TAU_S = 30.0 # 정상 호흡 진폭 기준선 EMA 시상수, 무호흡 검출에 사용


class AmplitudeDetector(Detector):
    """한 샘플씩 흡기/호기 onset·무호흡을 판정하는 인과적 진폭 히스테리시스 검출기."""

    INHALE = "inhale"
    EXHALE = "exhale"
    UNKNOWN = "unknown"

    def __init__(self, k_delta=K_DELTA, delta_floor=DELTA_FLOOR,
                 min_phase_s=MIN_PHASE_S, settle_s=SETTLE_S,
                 env_decay_s=ENV_DECAY_S, polarity=POLARITY,
                 apnea_frac=APNEA_FRAC, apnea_min_s=APNEA_MIN_S,
                 baseline_tau_s=BASELINE_TAU_S,
                 rate_window=RATE_WINDOW, rate_min_s=RATE_MIN_S, rate_max_s=RATE_MAX_S):
        self.k_delta = k_delta
        self.delta_floor = delta_floor
        self.min_phase_s = min_phase_s
        self.settle_s = settle_s
        self.env_decay_s = env_decay_s
        self.polarity = polarity
        self.apnea_frac = apnea_frac
        self.apnea_min_s = apnea_min_s
        self.baseline_tau_s = baseline_tau_s
        self.rate_window = rate_window
        self.rate_min_s = rate_min_s
        self.rate_max_s = rate_max_s

        self.t0 = None
        self.last_t = None
        self.phase = self.UNKNOWN
        self.env_hi = None
        self.env_lo = None
        self.peak = None
        self.trough = None
        self.peak_t = None
        self.trough_t = None
        self.peak_yraw = None
        self.trough_yraw = None
        self.last_transition_t = None
        self.inhale_onsets = []

        self.baseline_amp = None
        self.low_amp_since = None
        self.in_apnea = False

    def _amplitude(self):
        if self.env_hi is None or self.env_lo is None:
            return 0.0
        return self.env_hi - self.env_lo

    def update(self, t, y_raw):
        y = self.polarity * y_raw
        if self.t0 is None:
            self.t0 = t
            self.last_t = t
            self.env_hi = self.env_lo = self.peak = self.trough = y
            self.peak_t = self.trough_t = t
            self.peak_yraw = self.trough_yraw = y_raw
            self.last_transition_t = t
            return None

        dt = t - self.last_t
        if dt <= 0:
            dt = 0.0
        self.last_t = t

        # 누설 포락선(적응형 peak-to-peak)
        amp0 = self._amplitude()
        decay = (amp0 if amp0 > 0 else 1.0) * dt / max(self.env_decay_s, 1e-6)
        self.env_hi = max(y, self.env_hi - decay)
        self.env_lo = min(y, self.env_lo + decay)
        amp = self._amplitude()

        event = self._update_apnea(t, dt, amp)

        if t - self.t0 < self.settle_s:
            self.peak = self.trough = y
            self.peak_t = self.trough_t = t
            self.peak_yraw = self.trough_yraw = y_raw
            return event

        delta = max(self.k_delta * amp, self.delta_floor)
        can_switch = (t - self.last_transition_t) >= self.min_phase_s

        if self.phase == self.UNKNOWN:
            self.peak = self.trough = y
            self.peak_t = self.trough_t = t
            self.peak_yraw = self.trough_yraw = y_raw
            self.phase = self.EXHALE
            return event

        if self.phase == self.EXHALE:
            if y < self.trough:
                self.trough, self.trough_t, self.trough_yraw = y, t, y_raw
            if can_switch and (y - self.trough) > delta and not self.in_apnea:
                self.phase = self.INHALE
                self.last_transition_t = t
                self.inhale_onsets.append(t)
                ev = {"type": "inhale_onset", "t": t, "y": y_raw,
                      "ext_t": self.trough_t, "ext_y": self.trough_yraw}
                self.peak, self.peak_t, self.peak_yraw = y, t, y_raw
                return ev
        else:  # INHALE
            if y > self.peak:
                self.peak, self.peak_t, self.peak_yraw = y, t, y_raw
            if can_switch and (self.peak - y) > delta and not self.in_apnea:
                self.phase = self.EXHALE
                self.last_transition_t = t
                ev = {"type": "exhale_onset", "t": t, "y": y_raw,
                      "ext_t": self.peak_t, "ext_y": self.peak_yraw}
                self.trough, self.trough_t, self.trough_yraw = y, t, y_raw
                return ev

        return event

    def _update_apnea(self, t, dt, amp):
        if self.baseline_amp is None:
            self.baseline_amp = amp
        elif not self.in_apnea and t - self.t0 >= self.settle_s:
            a = max(1e-4, min(1.0, dt / self.baseline_tau_s))
            self.baseline_amp += a * (amp - self.baseline_amp)

        if self.baseline_amp is None or self.baseline_amp <= 0:
            return None

        low = amp < self.apnea_frac * self.baseline_amp
        if not self.in_apnea:
            if low:
                if self.low_amp_since is None:
                    self.low_amp_since = t
                elif t - self.low_amp_since >= self.apnea_min_s:
                    self.in_apnea = True
                    return {"type": "apnea_start", "t": self.low_amp_since, "y": 0.0}
            else:
                self.low_amp_since = None
        else:
            if not low:
                self.in_apnea = False
                self.low_amp_since = None
                return {"type": "apnea_end", "t": t, "y": 0.0}
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
