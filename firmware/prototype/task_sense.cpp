#include "task_sense.h"

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_types.h"
#include "board_config.h"
#include "breath_filter.h"
#include "breath_slope.h"

static BandpassButter2 bp;
static SlopeDetector   det;

static int readAveragedMv(int pin, int n = AVG_N) {
    uint32_t sum = 0;
    for (int i = 0; i < n; i++) sum += analogReadMilliVolts(pin);
    return sum / n;
}

// 실제 표본화 주기를 스스로 잰다. 여기서는 재기만 하고, 출력은 앱 태스크가 한다.
// avg 가 PERIOD_MS 에 붙고 min/max 가 avg 근처면 정상.
static void rateTick(SenseUpdate *u) {
    static uint32_t prev = 0, n = 0, sum = 0, p_min = 0xFFFFFFFF, p_max = 0;
    const uint32_t now = micros();
    if (prev) {
        const uint32_t p = now - prev;  // 부호 없는 뺄셈이라 micros() 순환(약 71분)에도 안전
        sum += p; n++;
        if (p < p_min) p_min = p;
        if (p > p_max) p_max = p;
        if (n >= 1000 / PERIOD_MS) {                  // 약 1초치
            u->rate_fs     = 1e6f * n / sum;
            u->rate_avg_us = sum / n;
            u->rate_min_us = p_min;
            u->rate_max_us = p_max;
            u->rate_ready  = 1;
            n = 0; sum = 0; p_min = 0xFFFFFFFF; p_max = 0;
        }
    }
    prev = now;
}

static uint8_t eventBit(BreathEventType t) {
    switch (t) {
        case BR_INHALE_ONSET: return E_INHALE;
        case BR_EXHALE_ONSET: return E_EXHALE;
        case BR_SIGNAL_LOST:  return E_NOSIG;
        case BR_SIGNAL_OK:    return E_SIGOK;
        default:              return 0;
    }
}

void sense_reset() {
    bandpass_init(&bp, FS_HZ, HP_HZ, LP_HZ);
    slope_init(&det);
}

// 한 샘플치 처리. 이벤트·진단 플래그는 큐로 나갈 때까지 u 에 누적된다.
void sense_step(int16_t raw, int16_t mv, SenseUpdate *u) {
    u->raw = raw;
    u->mv  = mv;

    const bfloat y = bandpass_update(&bp, (bfloat)mv);

    BreathEvent ev;
    if (slope_update(&det, y, &ev)) {
        u->events |= eventBit(ev.type);
        u->ev_n   = ev.n;            // det.n 이 아니다 — slope_update 가 이미 ++ 했다
        u->ext_n  = ev.ext_n;
        u->ev_amp = (float)ev.amp;
    }

    u->n     = det.n;
    u->phase = (int8_t)det.phase;
    u->flags = (uint8_t)((slope_settled(&det) ? F_SETTLED : 0) |
                         (det.signal_ok       ? F_SIGOK   : 0));
    u->amp   = (float)slope_amplitude(&det);
    u->bpm   = (float)slope_bpm(&det);   // 확정 직후에 읽어야 기존과 같은 값이 나온다
}

static void sense_task(void *) {
    // 주기의 기준점은 태스크가 실제로 시작하는 순간에 잡는다.
    // setup() 에서 잡으면 태스크 생성까지의 간격만큼 첫 주기가 밀린다.
    TickType_t next = xTaskGetTickCount();
    SenseUpdate u = {};

    for (;;) {
        rateTick(&u);

        const int16_t raw = (int16_t)analogRead(SENSOR_PIN);
        const int16_t mv  = (int16_t)readAveragedMv(SENSOR_PIN);
        sense_step(raw, mv, &u);

        // 큐가 가득 찼을 때 대기시간 0 — 큐가 차도 여기서 멈추지 않는다.
        // 앱 태스크가 밀린 것을 센서가 기다려 해결할 문제가 아니라, 결함으로 보고할 문제다.
        if (xQueueSend(q_sense, &u, 0) == pdTRUE) {
            u.events = 0;
            u.rate_ready = 0;
            u.drops = 0;
        } else if (u.drops < 0xFFFF) {
            u.drops++;                      // 이벤트·진단은 지운 적 없으니 다음 틱에 합쳐 간다
        }

        vTaskDelayUntil(&next, pdMS_TO_TICKS(PERIOD_MS));
    }
}

void sense_start() {
    sense_reset();
    xTaskCreatePinnedToCore(sense_task, "sense", SENSE_STACK, nullptr,
                            SENSE_PRIO, nullptr, SENSE_CORE);
}
