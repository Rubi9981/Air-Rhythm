"""디지털 필터·스무딩 — 순수 DSP. 모두 인과적(미래 샘플 미참조).

  - 스무딩:     moving_average_causal, ema
  - 1-pole 대역통과: bandpass_1pole (매 샘플 실제 dt 로 계수 재계산)
  - 2차 Butterworth: Biquad → BandpassButter2 (한 샘플씩) → bandpass_butter2 (배열 래퍼)

배열 API 와 스트리밍 API 는 같은 구현을 공유한다. 실시간 시리얼 입력과 CSV
재생이 같은 결과를 내는 것이 이 구조로 보장된다.
"""

import math
from collections import deque


# -----------------------------------------------------------------------------
# 스무딩
# -----------------------------------------------------------------------------
def moving_average_causal(values, window):
    """실시간(인과적) 단순 이동평균: 과거 window개만 평균. 약 (window-1)/2 지연."""
    if window < 2:
        return list(values)
    out = []
    buf = deque(maxlen=window)
    for v in values:
        buf.append(v)
        out.append(sum(buf) / len(buf))
    return out


def ema(values, alpha):
    """지수이동평균: s = alpha*x + (1-alpha)*s. 상태값 1개 → 펌웨어용."""
    out = []
    s = None
    for v in values:
        s = v if s is None else alpha * v + (1.0 - alpha) * s
        out.append(s)
    return out


def resolve_alpha(smooth, ema_alpha=None):
    """EMA 계수 결정. None 이면 창 크기와 등가인 2/(N+1)."""
    if ema_alpha is not None:
        return ema_alpha
    n = max(2, smooth)
    return 2.0 / (n + 1.0)


# -----------------------------------------------------------------------------
# 1-pole 대역통과 (고역통과 → 저역통과 캐스케이드)
# -----------------------------------------------------------------------------
def _sample_dts(times, default_dt):
    """인접 샘플 간격 리스트. 첫 샘플·비정상 간격은 default 로 대체."""
    dts = []
    for i in range(len(times)):
        if i == 0:
            dts.append(default_dt)
            continue
        dt = times[i] - times[i - 1]
        if not 0.0 < dt <= 0.5:
            dt = default_dt
        dts.append(dt)
    return dts


def bandpass_1pole(times, values, hp_hz, lp_hz, default_dt=None):
    """1-pole 대역통과(인과적). 매 샘플 실제 dt 로 계수를 다시 계산한다.

      고역통과: tau=1/(2π·fc), a=tau/(tau+dt), y=a·(y_prev + x - x_prev)
      저역통과: a=1-exp(-2π·fc·dt),          y+=a·(x - y)
    출력은 드리프트가 제거되어 0 을 중심으로 진동한다.
    """
    if default_dt is None:
        diffs = sorted(t2 - t1 for t1, t2 in zip(times, times[1:]) if t2 > t1)
        default_dt = diffs[len(diffs) // 2] if diffs else 0.02

    dts = _sample_dts(times, default_dt)
    two_pi = 2.0 * math.pi
    out = []
    prev_x = None
    hp_prev_y = 0.0
    lp_y = None
    for x, dt in zip(values, dts):
        if prev_x is None:
            prev_x = x
            hp = 0.0
        else:
            tau = 1.0 / (two_pi * hp_hz)
            a_hp = tau / (tau + dt)
            hp = a_hp * (hp_prev_y + x - prev_x)
            prev_x = x
            hp_prev_y = hp
        if lp_y is None:
            lp_y = hp
        else:
            a_lp = 1.0 - math.exp(-two_pi * lp_hz * dt)
            lp_y += a_lp * (hp - lp_y)
        out.append(lp_y)
    return out


# -----------------------------------------------------------------------------
# 2차 Butterworth 대역통과 (codex butterworth_z.py 이식)
# -----------------------------------------------------------------------------
class Biquad:
    """Direct Form II transposed 2차 IIR 한 단."""

    def __init__(self, b0, b1, b2, a1, a2):
        self.b0, self.b1, self.b2 = b0, b1, b2
        self.a1, self.a2 = a1, a2
        self.state_1 = 0.0
        self.state_2 = 0.0
        self.initialized = False

    def initialize_steady_state(self, x):
        """입력 x 가 계속 들어왔다고 가정한 상태로 초기화(시작 과도응답 제거)."""
        steady_gain = (self.b0 + self.b1 + self.b2) / (1.0 + self.a1 + self.a2)
        y = steady_gain * x
        self.state_2 = self.b2 * x - self.a2 * y
        self.state_1 = self.b1 * x - self.a1 * y + self.state_2
        self.initialized = True

    def update(self, x):
        if not self.initialized:
            self.initialize_steady_state(x)
        y = self.b0 * x + self.state_1
        self.state_1 = self.b1 * x - self.a1 * y + self.state_2
        self.state_2 = self.b2 * x - self.a2 * y
        return y


def _butterworth_common(cutoff_hz, sample_rate_hz):
    if not 0.0 < cutoff_hz < sample_rate_hz / 2.0:
        raise ValueError("cutoff must satisfy 0 < cutoff < Nyquist")
    k = math.tan(math.pi * cutoff_hz / sample_rate_hz)
    norm = 1.0 / (1.0 + math.sqrt(2.0) * k + k * k)
    return k, norm


def butterworth2_lowpass(cutoff_hz, sample_rate_hz):
    """2차 Butterworth 저역통과. scipy.signal.butter(2, ..., 'low')와 동일 계수."""
    k, norm = _butterworth_common(cutoff_hz, sample_rate_hz)
    b0 = k * k * norm
    return Biquad(b0, 2.0 * b0, b0,
                  2.0 * (k * k - 1.0) * norm,
                  (1.0 - math.sqrt(2.0) * k + k * k) * norm)


def butterworth2_highpass(cutoff_hz, sample_rate_hz):
    """2차 Butterworth 고역통과. scipy.signal.butter(2, ..., 'high')와 동일 계수."""
    k, norm = _butterworth_common(cutoff_hz, sample_rate_hz)
    return Biquad(norm, -2.0 * norm, norm,
                  2.0 * (k * k - 1.0) * norm,
                  (1.0 - math.sqrt(2.0) * k + k * k) * norm)


class BandpassButter2:
    """2차 Butterworth 대역통과 — 한 샘플씩(스트리밍). 상태는 biquad 2개뿐.

    실시간 경로(시리얼)와 오프라인 경로(CSV 재생)가 이 클래스를 함께 쓰므로
    두 경로의 계산이 구조적으로 동일하다. 배열이 필요하면 bandpass_butter2().
    """

    def __init__(self, sample_rate_hz, hp_hz, lp_hz):
        self.high_pass = butterworth2_highpass(hp_hz, sample_rate_hz)
        self.low_pass = butterworth2_lowpass(lp_hz, sample_rate_hz)

    def update(self, x):
        """샘플 하나를 통과시킨다. 미래 샘플을 참조하지 않는다."""
        return self.low_pass.update(self.high_pass.update(x))


def bandpass_butter2(values, sample_rate_hz, hp_hz, lp_hz):
    """2차 Butterworth 대역통과(인과적): 고역통과 biquad → 저역통과 biquad.

    BandpassButter2 를 배열 전체에 적용하는 편의 래퍼.
    """
    bp = BandpassButter2(sample_rate_hz, hp_hz, lp_hz)
    return [bp.update(x) for x in values]


def estimate_sample_rate(times):
    """인접 간격의 중앙값으로 표본화 주파수를 추정(biquad 설계용)."""
    diffs = sorted(t2 - t1 for t1, t2 in zip(times, times[1:]) if t2 > t1)
    dt = diffs[len(diffs) // 2] if diffs else 0.02
    return 1.0 / dt
