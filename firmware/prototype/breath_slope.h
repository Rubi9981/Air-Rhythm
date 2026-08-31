// 기울기 기반 흡기/호기 검출 — breath/detectors/slope.py 의 C 이식.
//
// 평활된 기울기의 부호 전환으로 트리거한다. 극점 직후 기울기는 0을 빠르게 지나므로
// 작은 문턱을 금방 넘어 확정이 극점에 가깝다. 미래 샘플을 참조하지 않는다.
//
// 정확도 장치: 적응형 데드밴드(K_SLOPE) / 최소 위상시간 잠금(MIN_PHASE_N) /
//             중점 게이트(MID_GATE) / 진폭 하한(MIN_AMP).
//
// 파이썬과의 차이:
//   - 시간을 초가 아니라 샘플 수로 다룬다(dt 가 상수라 EMA 계수도 상수).
//   - 흡기 시각을 무한 리스트가 아니라 ONSET_RING 크기 링버퍼로 들고 있다(bpm 용).
// 무호흡 판정은 포함하지 않는다.

#ifndef BREATH_SLOPE_H
#define BREATH_SLOPE_H

#include <stdint.h>

#include "breath_config.h"

typedef enum {
    BR_NONE = 0,
    BR_INHALE_ONSET,
    BR_EXHALE_ONSET,
    BR_SIGNAL_LOST,
    BR_SIGNAL_OK
} BreathEventType;

typedef struct {
    BreathEventType type;
    uint32_t n;        // 확정 시점(샘플 번호)
    uint32_t ext_n;    // 실제 극점(샘플 번호). 지연 = n - ext_n
    bfloat y;          // 확정 시점의 대역통과 값
    bfloat ext_y;      // 극점에서의 대역통과 값
    bfloat amp;        // 그때의 추정 진폭
} BreathEvent;

#define BR_RISING   (+1)
#define BR_FALLING  (-1)
#define BR_UNKNOWN  (0)

// 각 필드의 자세한 뜻은 breath_slope.cpp 맨 위 "이름 풀이" 블록에 모아 두었다.
typedef struct {
    uint32_t n;                 // 지금까지 처리한 샘플 수
    int started;                // 첫 샘플로 상태를 시드했는가

    bfloat sd;                  // 평활 기울기(mV/s). 검출 지연을 지배한다
    bfloat avg_abs_sd;          // |sd| 의 장기 평균 — 적응형 문턱의 기준선
    bfloat y_prev;              // 직전 샘플의 y(극성 보정 후) — 1차 차분용
    bfloat env_hi, env_lo;      // 누설 포락선의 위/아래. amp = env_hi - env_lo

    int phase;                  // BR_RISING=흡기 중 / BR_FALLING=호기 중 / BR_UNKNOWN
    bfloat ext_val, ext_yraw;   // 현재 구간의 극값 (극성 보정 후 / 원본)
    uint32_t ext_n;             // 그 극값의 샘플 번호. 검출 지연 = 확정 n - ext_n
    uint32_t last_transition_n; // 마지막 전환 샘플 번호 — MIN_PHASE_N 잠금용

    int signal_ok;              // 무신호 판정 상태

    uint32_t onsets[ONSET_RING];  // 최근 흡기 시각(샘플) — 링버퍼
    uint8_t onset_head, onset_count;
} SlopeDetector;

void slope_init(SlopeDetector *d);

// 대역통과된 한 샘플을 넣는다. 이벤트가 있으면 *ev 에 채우고 1, 없으면 0.
int slope_update(SlopeDetector *d, bfloat y_raw, BreathEvent *ev);

bfloat slope_amplitude(const SlopeDetector *d);   // 현재 추정 진폭(p-p)
int slope_settled(const SlopeDetector *d);        // 정착 구간이 끝났는가
// 분당 호흡수. 판정 불가면 0 을 돌려준다.
bfloat slope_bpm(const SlopeDetector *d);

#endif  // BREATH_SLOPE_H
