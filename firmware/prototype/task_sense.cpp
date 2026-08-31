#include "task_sense.h"

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_types.h"
#include "board_config.h"
#include "breath_filter.h"
#include "breath_slope.h"

static BandpassButter2 bandpass;
static SlopeDetector   detector;

static int read_averaged_mv(int pin, int n = ADC_AVG_COUNT) {
    uint32_t sum = 0;
    for (int i = 0; i < n; i++) sum += analogReadMilliVolts(pin);
    return sum / n;
}

// 실제 표본화 주기(µs)를 스스로 잰다. 여기서는 재기만 하고, 출력은 앱 태스크가 한다.
// avg 가 PERIOD_MS 에 붙고 min/max 가 avg 근처면 정상.
static void measure_sample_period(SenseUpdate *pending) {
    // 이 함수의 시간 변수는 전부 마이크로초다. 코드베이스에 ms(PERIOD_MS)·µs·
    // 샘플 수(SETTLE_N)가 섞여 있으므로 이름에 단위를 남긴다.
    static uint32_t prev_us = 0, count = 0, sum_us = 0,
                    min_us = 0xFFFFFFFF, max_us = 0;
    const uint32_t now_us = micros();
    if (prev_us) {
        const uint32_t period_us = now_us - prev_us;  // 부호 없는 뺄셈이라 micros() 순환(약 71분)에도 안전
        sum_us += period_us; count++;
        if (period_us < min_us) min_us = period_us;
        if (period_us > max_us) max_us = period_us;
        if (count >= 1000 / PERIOD_MS) {              // 약 1초치
            pending->rate_fs     = 1e6f * count / sum_us;
            pending->rate_avg_us = sum_us / count;
            pending->rate_min_us = min_us;
            pending->rate_max_us = max_us;
            pending->rate_ready  = 1;
            count = 0; sum_us = 0; min_us = 0xFFFFFFFF; max_us = 0;
        }
    }
    prev_us = now_us;
}

static uint8_t event_to_bit(BreathEventType t) {
    switch (t) {
        case BR_INHALE_ONSET: return EVENT_INHALE;
        case BR_EXHALE_ONSET: return EVENT_EXHALE;
        case BR_SIGNAL_LOST:  return EVENT_SIGNAL_LOST;
        case BR_SIGNAL_OK:    return EVENT_SIGNAL_OK;
        default:              return 0;
    }
}

void sense_reset() {
    bandpass_init(&bandpass, FS_HZ, HP_HZ, LP_HZ);
    slope_init(&detector);
}

// 한 샘플치 처리. 이벤트·진단 플래그는 큐로 나갈 때까지 pending 에 누적된다.
void sense_step(int16_t raw, int16_t mv, SenseUpdate *pending) {
    pending->raw = raw;
    pending->mv  = mv;

    const bfloat y = bandpass_update(&bandpass, (bfloat)mv);

    BreathEvent ev;
    if (slope_update(&detector, y, &ev)) {
        pending->events |= event_to_bit(ev.type);
        pending->ev_n   = ev.n;            // detector.n 이 아니다 — slope_update 가 이미 ++ 했다
        pending->ext_n  = ev.ext_n;
        pending->ev_amp = (float)ev.amp;
    }

    pending->n     = detector.n;
    pending->phase = (int8_t)detector.phase;
    pending->flags = (uint8_t)((slope_settled(&detector) ? FLAG_SETTLED : 0) |
                         (detector.signal_ok       ? FLAG_SIGNAL_OK   : 0));
    pending->amp   = (float)slope_amplitude(&detector);
    pending->bpm   = (float)slope_bpm(&detector);   // 확정 직후에 읽어야 기존과 같은 값이 나온다
}

static void sense_task(void *) {
    // 주기의 기준점은 태스크가 실제로 시작하는 순간에 잡는다.
    // setup() 에서 잡으면 태스크 생성까지의 간격만큼 첫 주기가 밀린다.
    TickType_t next = xTaskGetTickCount();
    SenseUpdate pending = {};

    for (;;) {
        measure_sample_period(&pending);

        const int16_t raw = (int16_t)analogRead(SENSOR_PIN);
        const int16_t mv  = (int16_t)read_averaged_mv(SENSOR_PIN);
        sense_step(raw, mv, &pending);

        // 큐가 가득 찼을 때 대기시간 0 — 큐가 차도 여기서 멈추지 않는다.
        // 앱 태스크가 밀린 것을 센서가 기다려 해결할 문제가 아니라, 결함으로 보고할 문제다.
        if (xQueueSend(q_sense, &pending, 0) == pdTRUE) {
            pending.events = 0;
            pending.rate_ready = 0;
            pending.drops = 0;
        } else if (pending.drops < 0xFFFF) {
            pending.drops++;                      // 이벤트·진단은 지운 적 없으니 다음 틱에 합쳐 간다
        }

        vTaskDelayUntil(&next, pdMS_TO_TICKS(PERIOD_MS));
    }
}

void sense_start() {
    sense_reset();
    xTaskCreatePinnedToCore(sense_task, "sense", SENSE_STACK, nullptr,
                            SENSE_PRIO, nullptr, SENSE_CORE);
}
