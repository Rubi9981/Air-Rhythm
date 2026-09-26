#include "act_motor.h"

#include <Arduino.h>

#include "board_config.h"
#include "link_msg.h"

// 마지막으로 내보낸 값. 같은 값이면 GPIO 를 다시 건드리지 않는다.
// app_task 가 매 틱(50Hz) 부르므로 이게 없으면 초당 50번씩 같은 값을 쓰게 된다.
static uint8_t s_duty = 0;

void motor_init() {
    // 순서가 중요하다. 브레이크를 먼저 확정하고 나서 PWM 을 붙인다 —
    // 반대로 하면 LEDC 가 붙는 순간부터 브레이크 핀이 뜬 상태로 남는 구간이 생긴다.
    pinMode(PIN_MOTOR_BRAKE, OUTPUT);
    digitalWrite(PIN_MOTOR_BRAKE, MOTOR_BRAKE_ON);

    ledcAttach(PIN_MOTOR_PWM, MOTOR_PWM_HZ, MOTOR_PWM_BITS);
#if MOTOR_PWM_INVERTED
    // BLC-20 은 HIGH 를 정지로 읽는다. 하드웨어 반전을 걸어 두면
    // 이 파일 바깥에서는 "duty 0 = 정지, 255 = 최대" 라는 상식이 그대로 통한다.
    //
    // ledcAttach 와 이 호출 사이에는 출력이 LOW(= 전속 지령)다. 함수 두 개 사이라
    // 수 µs 이고 BLDC 는 그 시간에 뜨지 않지만, 부팅 전체로 보면 더 긴 구간이
    // 따로 있다 — GPIO 가 뜨는 구간은 외부 풀업으로 막아야 한다(아래 주석 참조).
    ledcOutputInvert(PIN_MOTOR_PWM, true);
#endif
    ledcWrite(PIN_MOTOR_PWM, 0);   // 반전 후에는 상시 HIGH = 정지

    s_duty = 0;
}

void motor_set(uint8_t duty) {
    if (duty == s_duty) return;          // 바뀐 게 없으면 아무것도 하지 않는다

    if (duty == 0) {
        // 정지: 먼저 전원을 끊고, 그다음 제동을 건다.
        // 순서를 바꾸면 PWM 이 살아 있는 동안 코일이 단락되는 구간이 생긴다.
        ledcWrite(PIN_MOTOR_PWM, 0);
        digitalWrite(PIN_MOTOR_BRAKE, MOTOR_BRAKE_ON);
    } else {
        // 기동: 제동을 먼저 풀고 나서 속도를 준다.
        digitalWrite(PIN_MOTOR_BRAKE, MOTOR_BRAKE_OFF);
        ledcWrite(PIN_MOTOR_PWM, duty);
    }

    s_duty = duty;
}

uint8_t motor_get() { return s_duty; }

// ---------------------------------------------------------------------------
// 자가진단 — 오실로스코프 없이 출력 확인
// ---------------------------------------------------------------------------

void motor_selftest() {
    // 1) LEDC 레지스터 되읽기. 점퍼가 없어도 나온다.
    //    ledcReadFreq() 는 실제로 잡힌 주파수라 분주 한계 때문에 설정값과 조금 다를 수 있다.
    const uint32_t reg_duty = ledcRead(PIN_MOTOR_PWM);
    const uint32_t reg_freq = ledcReadFreq(PIN_MOTOR_PWM);
    msg_emitf(SINK_SERIAL, "# SELFTEST pwm_pin=%d reg_duty=%lu/%d reg_freq=%luHz",
              PIN_MOTOR_PWM, (unsigned long)reg_duty,
              (1 << MOTOR_PWM_BITS) - 1, (unsigned long)reg_freq);

    // 2) 루프백 입력을 마구 찍어 HIGH 비율을 센다.
    //    PWM 과 동기를 맞추지 않으므로 사실상 무작위 표본이고, 400주기를 훑으면
    //    비율이 듀티로 수렴한다. 정확한 파형이 아니라 "듀티가 맞는가" 를 보는 것이다.
    pinMode(PIN_MOTOR_PWM_LOOPBACK, INPUT);
    pinMode(PIN_MOTOR_BRAKE_LOOPBACK, INPUT);

    uint32_t hi = 0, total = 0;
    const uint32_t t0 = micros();
    while (micros() - t0 < (uint32_t)SELFTEST_WINDOW_MS * 1000u) {
        if (digitalRead(PIN_MOTOR_PWM_LOOPBACK)) hi++;
        total++;
    }

    const float pad_pct = total ? (100.0f * (float)hi / (float)total) : 0.0f;
    // 분모는 2^bits 이지 2^bits-1 이 아니다. LEDC 는 카운터가 0..2^bits-1 을 도는 동안
    // "counter < duty" 일 때만 HIGH 라서, duty=255(8비트)는 256분의 255 = 99.6% 다.
    float want_pct = 100.0f * (float)reg_duty / (float)(1u << MOTOR_PWM_BITS);
#if MOTOR_PWM_INVERTED
    want_pct = 100.0f - want_pct;   // 핀에서는 뒤집혀 나간다
#endif
    msg_emitf(SINK_SERIAL, "# SELFTEST pad_duty=%.1f%% (기대 %.1f%%) samples=%lu%s",
              pad_pct, want_pct, (unsigned long)total,
              MOTOR_PWM_INVERTED ? "  [역논리: HIGH=정지]" : "");

    // 3) 브레이크 핀 레벨
    const int brake = digitalRead(PIN_MOTOR_BRAKE_LOOPBACK);
    msg_emitf(SINK_SERIAL, "# SELFTEST brake_pin=%d pad=%s (%s)",
              PIN_MOTOR_BRAKE, brake ? "HIGH" : "LOW",
              brake == MOTOR_BRAKE_ON ? "제동" : "해제");

    // 기대와 실측이 크게 어긋나면 점퍼가 없거나 엉뚱한 핀에 꽂힌 것이다.
    const float err = pad_pct > want_pct ? pad_pct - want_pct : want_pct - pad_pct;
    if (err > 10.0f)
        msg_emit(SINK_SERIAL, "# SELFTEST 기대와 다름 — 루프백 점퍼 연결과 핀 번호를 확인하세요");
}
