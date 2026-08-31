#include <Arduino.h>
#include "breath_config.h"
#include "breath_filter.h"
#include "breath_slope.h"
#include "csv.h"
static const uint32_t PERIOD_MS = 20;
static BandpassButter2 bp; static SlopeDetector det;
static bool announced_settled = false;
static void reportEvent(const BreathEvent &ev) {
  const unsigned long delay_ms = (unsigned long)(ev.n - ev.ext_n) * PERIOD_MS;
  const float bpm = (float)slope_bpm(&det);
  switch (ev.type) {
    case BR_INHALE_ONSET:
    case BR_EXHALE_ONSET:
      Serial.printf("# %s n=%lu delay=%lums", ev.type == BR_INHALE_ONSET ? "INHALE" : "EXHALE",
                    (unsigned long)ev.n, delay_ms);
      if (bpm > 0.0f) Serial.printf(" bpm=%.1f", bpm);
      Serial.println(); break;
    case BR_SIGNAL_LOST: Serial.printf("# NOSIG n=%lu amp=%.1f\n", (unsigned long)ev.n, (float)ev.amp); break;
    case BR_SIGNAL_OK:   Serial.printf("# SIGOK n=%lu amp=%.1f\n", (unsigned long)ev.n, (float)ev.amp); break;
    default: break;
  }
}
int main(int argc, char **argv) {
  bandpass_init(&bp, FS_HZ, HP_HZ, LP_HZ); slope_init(&det);
  for (auto &r : read_csv(argv[1])) {
    int raw = r.raw, mv = r.mv;
    Serial.print(raw); Serial.print('\t'); Serial.println(mv);
    const bfloat y = bandpass_update(&bp, (bfloat)mv);
    BreathEvent ev;
    if (slope_update(&det, y, &ev)) reportEvent(ev);
    if (!announced_settled && slope_settled(&det)) { announced_settled = true; Serial.println("# SETTLED"); }
  }
  return 0;
}
