// 호흡 검출 상수 — 파이썬 구현과 짝을 이룬다.
//
// 값이 갈라지면 호스트 대조 검증(host_test)이 잡는다. 바꿀 때는 양쪽을 함께 고칠 것.
//   breath/config.py             : HP_HZ, LP_HZ
//   breath/detectors/slope.py    : SLOPE_TAU_S ~ POLARITY, RATE_*
//   scripts/monitor_breath.py    : FS, NO_SIGNAL_HYST
//
// 시간은 전부 샘플 수로 다룬다(고정 dt). 펌웨어가 vTaskDelayUntil 로 정확히 50Hz 를
// 지키므로 dt 가 상수이고, 그러면 EMA 계수도 상수가 되어 매 샘플 나눗셈이 사라진다.

#ifndef BREATH_CONFIG_H
#define BREATH_CONFIG_H

// 부동소수 타입 — 기기에서는 float. 호스트 검증 때 double 로 바꿔 정밀도 영향을 분리한다.
// (ESP32 는 double 이 소프트웨어 에뮬레이션이므로 기기 빌드는 반드시 float)
#ifdef BREATH_USE_DOUBLE
typedef double bfloat;
#else
typedef float bfloat;
#endif

// --- 표본화 ---
#define FS_HZ        50.0f     // 펌웨어 PERIOD_MS(20ms)와 일치
#define DT_S         (1.0f / FS_HZ)

// --- 대역통과 (breath/config.py) ---
#define HP_HZ        0.08f     // 고역: 약 5 bpm (드리프트 제거)
#define LP_HZ        0.70f     // 저역: 약 42 bpm (노이즈 제거)

// --- 기울기 트리거 (지연을 지배) ---
#define SLOPE_TAU_S  0.08f     // 기울기 평활 시상수(초)
#define AVG_TAU_S    8.0f      // 평균|기울기| 추정 시상수
#define K_SLOPE      0.25f     // 데드밴드 = K_SLOPE × 평균|기울기|
#define SLOPE_FLOOR  1.0f      // 데드밴드 절대 하한(mV/s)

// --- 정확도 장치 ---
#define MIN_PHASE_S  1.2f      // 전환 직후 반대 전환 금지(초)
#define MID_GATE     1         // 흡기는 포락선 중점 아래, 호기는 위에서만 허용
#define PROM_RATIO   0.0f      // 골/마루 되돌림 요구(진폭 대비). 0 = off
#define MIN_AMP      5.0f      // 최근 진폭(p-p)이 이보다 작으면 판정 보류(mV)
#define ENV_DECAY_S  6.0f      // 진폭 포락선 완화 시상수(초)

#define SETTLE_S     12.0f     // 시작 과도응답 구간(판정 보류)
#define POLARITY     (+1.0f)   // +1: 상승=흡기. 센서 반대로 붙였으면 -1

// --- 무신호 판정 (scripts/monitor_breath.py) ---
#define NO_SIGNAL_HYST 1.5f    // 복귀는 MIN_AMP × 이 배수를 넘어야 인정

// --- 호흡률 ---
#define RATE_WINDOW  5         // 최근 간격 몇 개의 중앙값
#define RATE_MIN_S   1.4f
#define RATE_MAX_S   12.0f
// 파이썬은 흡기 시각을 무한 리스트로 들고 간격을 거른 "뒤" 최근 5개를 쓴다.
// 펌웨어는 링버퍼라, 걸러지고도 5개가 남도록 넉넉히 잡는다.
#define ONSET_RING   12

// --- 시간 → 샘플 수 변환 (컴파일 상수) ---
#define SETTLE_N     ((uint32_t)(SETTLE_S   * FS_HZ))   // 600
#define MIN_PHASE_N  ((uint32_t)(MIN_PHASE_S * FS_HZ))  // 60
#define RATE_MIN_N   ((uint32_t)(RATE_MIN_S * FS_HZ))   // 70
#define RATE_MAX_N   ((uint32_t)(RATE_MAX_S * FS_HZ))   // 600

// --- EMA 계수 (컴파일 상수) ---
// 파이썬: a = dt / (tau + dt)   — 후진 오일러라 dt 가 커져도 항상 0<a<1
#define A_SLOPE      (DT_S / (SLOPE_TAU_S + DT_S))      // 0.2
#define A_AVG        (DT_S / (AVG_TAU_S   + DT_S))      // 0.00249377
#define ENV_DECAY_K  (DT_S / ENV_DECAY_S)               // 0.00333333

#endif  // BREATH_CONFIG_H
