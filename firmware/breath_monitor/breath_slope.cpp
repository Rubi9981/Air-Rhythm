#include "breath_slope.h"

#include <math.h>

void slope_init(SlopeDetector *d) {
  d->n = 0;
  d->started = 0;
  d->sd = (bfloat)0.0;
  d->avg_abs_sd = (bfloat)0.0;
  d->y_prev = (bfloat)0.0;
  d->env_hi = d->env_lo = (bfloat)0.0;
  d->phase = BR_UNKNOWN;
  d->ext_val = d->ext_yraw = (bfloat)0.0;
  d->ext_n = 0;
  d->last_transition_n = 0;
  d->signal_ok = 1;
  d->onset_head = d->onset_count = 0;
}

bfloat slope_amplitude(const SlopeDetector *d) {
  return d->env_hi - d->env_lo;
}

int slope_settled(const SlopeDetector *d) {
  return d->n >= SETTLE_N;
}

static void seed_phase(SlopeDetector *d, bfloat y, bfloat y_raw) {
  d->ext_val = y;
  d->ext_n = d->n;
  d->ext_yraw = y_raw;
}

static void push_onset(SlopeDetector *d, uint32_t n) {
  d->onsets[d->onset_head] = n;
  d->onset_head = (uint8_t)((d->onset_head + 1) % ONSET_RING);
  if (d->onset_count < ONSET_RING) d->onset_count++;
}

// 무신호 판정 — scripts/monitor_breath.py 의 check_signal() 이식.
// 검출기는 무신호 이벤트를 내지 않으므로(조용히 판정을 보류할 뿐) 진폭을 직접 본다.
// 복귀 문턱을 NO_SIGNAL_HYST 배로 두어 문턱 근처 깜빡임을 막는다.
static int check_signal(SlopeDetector *d, BreathEvent *ev) {
  const bfloat amp = slope_amplitude(d);
  if (d->signal_ok && amp < MIN_AMP) {
    d->signal_ok = 0;
    ev->type = BR_SIGNAL_LOST;
    ev->n = ev->ext_n = d->n;
    ev->y = ev->ext_y = (bfloat)0.0;
    ev->amp = amp;
    return 1;
  }
  if (!d->signal_ok && amp >= MIN_AMP * NO_SIGNAL_HYST) {
    d->signal_ok = 1;
    ev->type = BR_SIGNAL_OK;
    ev->n = ev->ext_n = d->n;
    ev->y = ev->ext_y = (bfloat)0.0;
    ev->amp = amp;
    return 1;
  }
  return 0;
}

static void emit(SlopeDetector *d, BreathEvent *ev, BreathEventType type,
                 bfloat y_raw, bfloat amp) {
  ev->type = type;
  ev->n = d->n;
  ev->ext_n = d->ext_n;
  ev->y = y_raw;
  ev->ext_y = d->ext_yraw;
  ev->amp = amp;
}

int slope_update(SlopeDetector *d, bfloat y_raw, BreathEvent *ev) {
  const bfloat y = (bfloat)POLARITY * y_raw;

  if (!d->started) {
    d->started = 1;
    d->y_prev = y;
    d->env_hi = d->env_lo = y;
    seed_phase(d, y, y_raw);
    d->last_transition_n = d->n;
    d->n++;
    return 0;
  }

  // 기울기(1차 차분) → 평활 → 평균|기울기|.  dt 가 상수라 계수도 상수.
  const bfloat diff = (y - d->y_prev) / DT_S;
  d->y_prev = y;
  d->sd += (bfloat)A_SLOPE * (diff - d->sd);
  d->avg_abs_sd += (bfloat)A_AVG * ((bfloat)fabs((double)d->sd) - d->avg_abs_sd);

  // 진폭 포락선(누설). 신호가 밖으로 나가면 즉시 끌려가고, 아니면 서서히 좁혀온다.
  const bfloat amp0 = slope_amplitude(d);
  const bfloat decay = (amp0 > (bfloat)0.0 ? amp0 : (bfloat)1.0) * (bfloat)ENV_DECAY_K;
  const bfloat hi = d->env_hi - decay;
  const bfloat lo = d->env_lo + decay;
  d->env_hi = (y > hi) ? y : hi;
  d->env_lo = (y < lo) ? y : lo;
  const bfloat amp = slope_amplitude(d);

  const bfloat k_sth = (bfloat)K_SLOPE * d->avg_abs_sd;
  const bfloat sth = (k_sth > (bfloat)SLOPE_FLOOR) ? k_sth : (bfloat)SLOPE_FLOOR;

  // 정착 전 / 무신호: 판정 보류(극값은 최신으로 유지)
  if (d->n < SETTLE_N || amp < MIN_AMP) {
    seed_phase(d, y, y_raw);
    d->phase = BR_UNKNOWN;
    const int had = check_signal(d, ev);   // 정착 이후에만 의미가 있다
    if (!slope_settled(d)) {
      d->signal_ok = 1;                    // 정착 중에는 상태만 초기화하고 알리지 않음
      d->n++;
      return 0;
    }
    d->n++;
    return had;
  }

  const int signal_ev = check_signal(d, ev);

  const uint32_t since = d->n - d->last_transition_n;
  const int can_switch = (since >= MIN_PHASE_N);

  // 중점 게이트 — 흡기 전환은 포락선 중점 아래, 호기 전환은 위에서만 허용
  int gate_open = 1;
#if MID_GATE
  {
    const bfloat mid = (bfloat)0.5 * (d->env_hi + d->env_lo);
    gate_open = (d->phase == BR_FALLING) ? (y < mid) : (y > mid);
  }
#endif

  if (d->phase == BR_UNKNOWN) {
    if (d->sd > sth) {
      d->phase = BR_RISING;
    } else if (d->sd < -sth) {
      d->phase = BR_FALLING;
    } else {
      d->n++;
      return signal_ev;
    }
    seed_phase(d, y, y_raw);
    d->last_transition_n = d->n;
    d->n++;
    return signal_ev;
  }

  if (d->phase == BR_FALLING) {
    if (y < d->ext_val) seed_phase(d, y, y_raw);          // 골 추적
    if (can_switch && gate_open && d->sd > sth &&
        (y - d->ext_val) >= (bfloat)PROM_RATIO * amp) {
      emit(d, ev, BR_INHALE_ONSET, y_raw, amp);
      push_onset(d, d->n);
      d->phase = BR_RISING;
      d->last_transition_n = d->n;
      seed_phase(d, y, y_raw);                            // 새 구간 극값 시드
      d->n++;
      return 1;
    }
  } else {  // BR_RISING
    if (y > d->ext_val) seed_phase(d, y, y_raw);          // 마루 추적
    if (can_switch && gate_open && d->sd < -sth &&
        (d->ext_val - y) >= (bfloat)PROM_RATIO * amp) {
      emit(d, ev, BR_EXHALE_ONSET, y_raw, amp);
      d->phase = BR_FALLING;
      d->last_transition_n = d->n;
      seed_phase(d, y, y_raw);
      d->n++;
      return 1;
    }
  }

  d->n++;
  return signal_ev;
}

bfloat slope_bpm(const SlopeDetector *d) {
  if (d->onset_count < 2) return (bfloat)0.0;

  // 링버퍼를 오래된 것부터 읽어 간격을 만들고, 범위 밖은 버린다.
  uint32_t iv[ONSET_RING];
  int m = 0;
  const int cnt = d->onset_count;
  const int start = (d->onset_count < ONSET_RING)
                        ? 0
                        : d->onset_head;  // 가장 오래된 위치
  uint32_t prev = d->onsets[start % ONSET_RING];
  for (int i = 1; i < cnt; i++) {
    const uint32_t cur = d->onsets[(start + i) % ONSET_RING];
    const uint32_t gap = cur - prev;
    prev = cur;
    if (gap >= RATE_MIN_N && gap <= RATE_MAX_N) iv[m++] = gap;
  }
  if (m == 0) return (bfloat)0.0;

  // 최근 RATE_WINDOW 개만 남긴다
  int from = (m > RATE_WINDOW) ? (m - RATE_WINDOW) : 0;
  int k = m - from;
  uint32_t buf[RATE_WINDOW];
  for (int i = 0; i < k; i++) buf[i] = iv[from + i];

  // 중앙값 (k <= 5 이므로 삽입정렬)
  for (int i = 1; i < k; i++) {
    const uint32_t v = buf[i];
    int j = i - 1;
    while (j >= 0 && buf[j] > v) { buf[j + 1] = buf[j]; j--; }
    buf[j + 1] = v;
  }
  // 파이썬 statistics.median: 짝수면 가운데 두 개의 평균
  const bfloat med = (k % 2) ? (bfloat)buf[k / 2]
                             : ((bfloat)buf[k / 2 - 1] + (bfloat)buf[k / 2]) * (bfloat)0.5;
  if (med <= (bfloat)0.0) return (bfloat)0.0;
  return (bfloat)60.0 * (bfloat)FS_HZ / med;
}
