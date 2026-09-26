#include "task_ui.h"

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_types.h"
#include "board_config.h"
#include "link_cmd.h"
#include "link_key.h"

// 핀 → 버튼. 핀 번호는 board_config.h 핀 배치에만 있다.
// 순서가 곧 보내는 순서다 — 같은 순간에 여러 버튼이 눌리면 OK 를 먼저 보낸다.
// 실행 화면에서 OK 는 정지이므로, 정지가 다른 입력보다 먼저 반영된다(목표 5.6).
static const struct { uint8_t pin; Button btn; } KEYS[] = {
    { PIN_KEY_OK,    BTN_OK    },
    { PIN_KEY_UP,    BTN_UP    },
    { PIN_KEY_DOWN,  BTN_DOWN  },
    { PIN_KEY_LEFT,  BTN_LEFT  },
    { PIN_KEY_RIGHT, BTN_RIGHT },
};
static_assert(sizeof KEYS / sizeof KEYS[0] == BTN_COUNT, "KEYS 표에 모든 버튼이 있어야 합니다");

static KeyDebouncer s_keys;

// 스위치 COM 이 GND 라 누르면 LOW 다. 눌린 버튼을 비트 1 로 모은다.
static uint8_t read_keys() {
    uint8_t pressed = 0;
    for (const auto &k : KEYS) {
        if (digitalRead(k.pin) == LOW) pressed |= (uint8_t)(1u << k.btn);
    }
    return pressed;
}

static void ui_task(void *) {
    TickType_t next = xTaskGetTickCount();

    for (;;) {
        const uint8_t pressed = key_debounce(&s_keys, read_keys(), millis());

        if (pressed) {
            for (const auto &k : KEYS) {
                if (!(pressed & (1u << k.btn))) continue;
                const Command cmd = { CMD_BUTTON, SRC_KEY, (int32_t)k.btn };
                cmd_submit(&cmd);             // 대기 0 — 큐가 가득 차면 이 입력은 버린다
            }
        }

        // TODO(5단계): 여기서 LCD 를 그린다. 그리는 동안에는 버튼을 읽지 않는다.

        vTaskDelayUntil(&next, pdMS_TO_TICKS(KEY_POLL_MS));
    }
}

void ui_start() {
    for (const auto &k : KEYS) pinMode(k.pin, INPUT_PULLUP);   // 외부 저항 없이 내부 풀업
    key_debounce_init(&s_keys, read_keys(), millis());          // 지금 눌려 있는 버튼은 입력이 아니다
    xTaskCreatePinnedToCore(ui_task, "ui", UI_STACK, nullptr, UI_PRIORITY, nullptr, UI_CORE);
}
