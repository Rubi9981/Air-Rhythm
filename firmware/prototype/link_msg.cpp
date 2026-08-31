#include "link_msg.h"

#include <Arduino.h>

#include "board_config.h"

static bool stalled = false;   // 센서 태스크 정지 상태. 진입·복귀에서 한 번씩만 알린다

static void reportRate(const SenseUpdate *s) {
    Serial.printf("# fs=%.2fHz avg=%luus min=%luus max=%luus\n",
                  s->rate_fs, (unsigned long)s->rate_avg_us,
                  (unsigned long)s->rate_min_us, (unsigned long)s->rate_max_us);
}

// 지연은 극점 → 확정까지 걸린 샘플 수. 오프라인 지표와 같은 값이다.
// ev_n 은 det.n 이 아니라 이벤트가 확정된 샘플이어야 한다(app_types.h 참조).
static void reportOnset(const SenseUpdate *s, const char *kind) {
    const unsigned long delay_ms = (unsigned long)(s->ev_n - s->ext_n) * PERIOD_MS;
    Serial.printf("# %s n=%lu delay=%lums", kind, (unsigned long)s->ev_n, delay_ms);
    if (s->bpm > 0.0f) Serial.printf(" bpm=%.1f", s->bpm);
    Serial.println();
}

static void reportSignal(const SenseUpdate *s, const char *kind) {
    Serial.printf("# %s n=%lu amp=%.1f\n", kind, (unsigned long)s->ev_n, s->ev_amp);
}

void msg_report(const SenseUpdate *s) {
    if (stalled) {                           // 샘플이 다시 오기 시작했다
        stalled = false;
        Serial.println("# FAULT sense_ok");
    }

    // 큐가 차서 버린 틱이 있었다. 정상 동작에서는 절대 나오지 않는 줄이다.
    // 이 경우 아래 이벤트들의 n/amp 는 마지막 것만 남아 있다.
    if (s->drops) Serial.printf("# QDROP n=%lu ticks=%u\n", (unsigned long)s->n, s->drops);

    if (REPORT_RATE && s->rate_ready) reportRate(s);

    if (REPORT_SAMPLE) {                     // Serial Plotter / monitor_breath.py 용
        Serial.print(s->raw); Serial.print('\t'); Serial.println(s->mv);
    }

    if (s->events & E_INHALE) reportOnset(s, "INHALE");
    if (s->events & E_EXHALE) reportOnset(s, "EXHALE");
    if (s->events & E_NOSIG)  reportSignal(s, "NOSIG");
    if (s->events & E_SIGOK)  reportSignal(s, "SIGOK");

    // 정착 완료는 한 번만 알린다 (기존 announced_settled 와 같은 동작).
    static bool announced_settled = false;
    if (!announced_settled && (s->flags & F_SETTLED)) {
        announced_settled = true;
        Serial.println("# SETTLED");
    }
}

void msg_sense_stall() {
    // 반복해서 쏟아지지 않도록 상태가 바뀔 때만 알린다.
    if (!stalled) {
        stalled = true;
        Serial.println("# FAULT sense_stall");
    }
}
