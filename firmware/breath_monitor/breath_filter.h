// 2차 Butterworth 대역통과 — breath/filters.py 의 C 이식.
//
// 원본: Biquad(Direct Form II transposed) → BandpassButter2(고역통과 → 저역통과).
// 계수는 쌍일차 변환으로 설계하며 fs 가 고정이라 setup() 에서 한 번만 계산한다.

#ifndef BREATH_FILTER_H
#define BREATH_FILTER_H

#include "breath_config.h"

typedef struct {
  bfloat b0, b1, b2, a1, a2;
  bfloat state_1, state_2;
  int initialized;
} Biquad;

// 계수 설계 (cutoff < fs/2 여야 한다)
void biquad_lowpass(Biquad *q, bfloat cutoff_hz, bfloat fs_hz);
void biquad_highpass(Biquad *q, bfloat cutoff_hz, bfloat fs_hz);

// 입력 x 가 계속 들어왔다고 가정한 상태로 초기화 — 시작 과도응답 제거.
// 원신호가 ~1000mV DC 라 이게 없으면 시작 직후 출력이 크게 튄다.
void biquad_init_steady(Biquad *q, bfloat x);

bfloat biquad_update(Biquad *q, bfloat x);

// 첫 샘플을 기준점으로 빼고 필터를 태운다.
//
// 고역통과가 어차피 DC 를 지우므로 출력은 수학적으로 동일하지만, float32 에서는
// 차이가 크다. 원신호가 ~1000mV 라 biquad 내부 상태가 ~2000 까지 커지는데,
// 고역통과 극점 반지름이 0.9929(단위원에 근접)라 반올림 오차가 1/(1-r) ≈ 140배로
// 증폭된다. 기준점을 빼면 상태가 0 근처에 머문다.
// 실측: 파이썬 대비 최대 오차 0.057mV → 0.0017mV (33배 개선).
typedef struct {
  Biquad hp, lp;
  bfloat offset;
  int have_offset;
} BandpassButter2;

void bandpass_init(BandpassButter2 *bp, bfloat fs_hz, bfloat hp_hz, bfloat lp_hz);
bfloat bandpass_update(BandpassButter2 *bp, bfloat x);

#endif  // BREATH_FILTER_H
