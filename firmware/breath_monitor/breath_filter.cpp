#include "breath_filter.h"

#include <math.h>

// 파이썬 _butterworth_common 과 동일: k = tan(pi*fc/fs), norm = 1/(1+sqrt2*k+k^2)
static void butter_common(bfloat cutoff_hz, bfloat fs_hz, bfloat *k, bfloat *norm) {
  const bfloat kk = (bfloat)tan((double)(M_PI * cutoff_hz / fs_hz));
  const bfloat s2 = (bfloat)M_SQRT2;
  *k = kk;
  *norm = (bfloat)1.0 / ((bfloat)1.0 + s2 * kk + kk * kk);
}

void biquad_lowpass(Biquad *q, bfloat cutoff_hz, bfloat fs_hz) {
  bfloat k, norm;
  butter_common(cutoff_hz, fs_hz, &k, &norm);
  const bfloat b0 = k * k * norm;
  q->b0 = b0;
  q->b1 = (bfloat)2.0 * b0;
  q->b2 = b0;
  q->a1 = (bfloat)2.0 * (k * k - (bfloat)1.0) * norm;
  q->a2 = ((bfloat)1.0 - (bfloat)M_SQRT2 * k + k * k) * norm;
  q->state_1 = q->state_2 = (bfloat)0.0;
  q->initialized = 0;
}

void biquad_highpass(Biquad *q, bfloat cutoff_hz, bfloat fs_hz) {
  bfloat k, norm;
  butter_common(cutoff_hz, fs_hz, &k, &norm);
  q->b0 = norm;
  q->b1 = (bfloat)-2.0 * norm;
  q->b2 = norm;
  q->a1 = (bfloat)2.0 * (k * k - (bfloat)1.0) * norm;
  q->a2 = ((bfloat)1.0 - (bfloat)M_SQRT2 * k + k * k) * norm;
  q->state_1 = q->state_2 = (bfloat)0.0;
  q->initialized = 0;
}

void biquad_init_steady(Biquad *q, bfloat x) {
  const bfloat steady_gain =
      (q->b0 + q->b1 + q->b2) / ((bfloat)1.0 + q->a1 + q->a2);
  const bfloat y = steady_gain * x;
  q->state_2 = q->b2 * x - q->a2 * y;
  q->state_1 = q->b1 * x - q->a1 * y + q->state_2;
  q->initialized = 1;
}

bfloat biquad_update(Biquad *q, bfloat x) {
  if (!q->initialized) biquad_init_steady(q, x);
  const bfloat y = q->b0 * x + q->state_1;
  q->state_1 = q->b1 * x - q->a1 * y + q->state_2;
  q->state_2 = q->b2 * x - q->a2 * y;
  return y;
}

void bandpass_init(BandpassButter2 *bp, bfloat fs_hz, bfloat hp_hz, bfloat lp_hz) {
  biquad_highpass(&bp->hp, hp_hz, fs_hz);
  biquad_lowpass(&bp->lp, lp_hz, fs_hz);
  bp->offset = (bfloat)0.0;
  bp->have_offset = 0;
}

bfloat bandpass_update(BandpassButter2 *bp, bfloat x) {
  if (!bp->have_offset) {          // 첫 샘플을 기준점으로 (헤더 주석 참조)
    bp->offset = x;
    bp->have_offset = 1;
  }
  return biquad_update(&bp->lp, biquad_update(&bp->hp, x - bp->offset));
}
