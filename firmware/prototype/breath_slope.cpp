#include "breath_slope.h"

#include <math.h>

// ---------------------------------------------------------------------------
// 이름 풀이
//
// 이 파일의 짧은 이름들은 breath/detectors/slope.py 와 1:1로 맞춰 둔 것이다.
// 한쪽만 바꾸면 두 구현을 나란히 놓고 읽을 수 없게 되고, 호스트 대조 검증의
// 가치도 떨어진다. 그래서 이름은 그대로 두고 뜻을 여기 모아 둔다.
//
//   d          SlopeDetector*.  파이썬의 self 에 해당한다
//
//   y_raw      대역통과 출력 그대로. 이벤트에 실려 밖으로 나가는 값
//   y          y_raw 에 POLARITY 를 곱한 값. 이 파일 안에서는 "상승 = 흡기"가
//              항상 참이 되도록 극성을 맞춘 뒤 판단한다
//   y_prev     직전 샘플의 y. 1차 차분에 쓴다
//
//   diff       (y - y_prev) / DT_S — 평활 전 순간 기울기 (mV/s)
//   sd         smoothed derivative. diff 를 SLOPE_TAU_S 로 EMA 평활한 값.
//              검출 지연을 지배하는 손잡이다
//   avg_abs_sd |sd| 의 장기 평균(AVG_TAU_S). 적응형 문턱의 기준선
//   k_sth      K_SLOPE × avg_abs_sd — 문턱의 적응형 항(하한 적용 전)
//   sth        slope threshold. max(k_sth, SLOPE_FLOOR). 이번 샘플의 데드밴드
//
//   env_hi     누설 포락선의 위쪽. y 가 위로 벗어나면 즉시 따라가고,
//   env_lo     아래쪽. 아니면 서로 서서히 좁혀온다(ENV_DECAY_S)
//   amp0       포락선을 갱신하기 "전"의 진폭. 좁혀오는 양을 진폭에 비례시켜
//              호흡이 커지든 작아지든 같은 속도로 수렴하게 한다
//   amp        갱신 후 진폭(peak-to-peak) = env_hi - env_lo
//   mid        포락선 중점 (env_hi + env_lo) / 2. MID_GATE 의 기준선.
//              0 이 아니라 중점을 쓰는 이유는 env_lo <= y <= env_hi 라서
//              y 가 매 호흡 반드시 중점을 가로지르기 때문이다(교착 불가)
//
//   ext_val    이번 구간의 극값(골 또는 마루)을 y 기준으로 담은 것
//   ext_yraw   같은 극값을 y_raw 기준으로. 이벤트 보고용
//   ext_n      그 극값이 나온 샘플 번호. 검출 지연 = 확정 n - ext_n
//   since      마지막 전환 이후 지난 샘플 수. MIN_PHASE_N 잠금에 쓴다
// ---------------------------------------------------------------------------

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
    // 파이썬은 intervals = [b - a for a, b in zip(onsets, onsets[1:])] 한 줄이다.
    uint32_t intervals[ONSET_RING];      // 걸러낸 흡기 간격(샘플 수)
    int kept = 0;                        // intervals 에 실제로 담긴 개수
    const int onset_count = d->onset_count;
    const int oldest = (d->onset_count < ONSET_RING)
                           ? 0
                           : d->onset_head;  // 링버퍼에서 가장 오래된 위치
    uint32_t prev_onset = d->onsets[oldest % ONSET_RING];
    for (int i = 1; i < onset_count; i++) {
        const uint32_t this_onset = d->onsets[(oldest + i) % ONSET_RING];
        const uint32_t gap = this_onset - prev_onset;
        prev_onset = this_onset;
        if (gap >= RATE_MIN_N && gap <= RATE_MAX_N) intervals[kept++] = gap;
    }
    if (kept == 0) return (bfloat)0.0;

    // 최근 RATE_WINDOW 개만 남긴다 — 파이썬의 intervals[-rate_window:]
    const int from = (kept > RATE_WINDOW) ? (kept - RATE_WINDOW) : 0;
    const int window_n = kept - from;    // 중앙값을 낼 표본 수 (<= RATE_WINDOW)
    uint32_t window[RATE_WINDOW];        // 최근 간격들 — 아래에서 정렬된다
    for (int i = 0; i < window_n; i++) window[i] = intervals[from + i];

    // 중앙값 (window_n <= 5 이므로 삽입정렬)
    for (int i = 1; i < window_n; i++) {
        const uint32_t v = window[i];
        int j = i - 1;
        while (j >= 0 && window[j] > v) { window[j + 1] = window[j]; j--; }
        window[j + 1] = v;
    }
    // 파이썬 statistics.median: 짝수면 가운데 두 개의 평균
    const bfloat median_gap = (window_n % 2)
        ? (bfloat)window[window_n / 2]
        : ((bfloat)window[window_n / 2 - 1] + (bfloat)window[window_n / 2]) * (bfloat)0.5;
    if (median_gap <= (bfloat)0.0) return (bfloat)0.0;
    return (bfloat)60.0 * (bfloat)FS_HZ / median_gap;
}
