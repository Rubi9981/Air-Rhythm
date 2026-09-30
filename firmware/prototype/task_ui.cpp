#include "task_ui.h"

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "app_types.h"
#include "board_config.h"
#include "link_cmd.h"
#include "link_key.h"

// ============================================================================
// 버튼
// ============================================================================

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

static void poll_keys() {
    const uint8_t pressed = key_debounce(&s_keys, read_keys(), millis());
    if (!pressed) return;
    for (const auto &k : KEYS) {
        if (!(pressed & (1u << k.btn))) continue;
        const Command cmd = { CMD_BUTTON, SRC_KEY, (int32_t)k.btn };
        cmd_submit(&cmd);                 // 대기 0 — 큐가 가득 차면 이 입력은 버린다
    }
}

// ============================================================================
// LCD1602 (PCF8574 I2C 백팩)
// ============================================================================

constexpr int         LCD_W               = 16;               // LCD1602 한 줄 글자 수
static const uint8_t  LCD_ADDRS[]         = { 0x27, 0x3F };  // 백팩 칩에 따라 둘 중 하나
static const uint32_t LCD_MIN_INTERVAL_MS = 200;    // 초당 최대 5번 갱신 (목표 7.5.3)
static const uint32_t LCD_RETRY_MS        = 2000;   // 응답이 없으면 이 간격으로 다시 찾는다
static const uint32_t LCD_BOOT_HOLD_MS    = 800;    // 부팅 화면을 읽을 수 있을 만큼은 보여준다
static const uint16_t I2C_TIMEOUT_MS      = 20;     // 기본 50ms 보다 짧게 — LCD 가 막혀도 버튼이 오래 멈추지 않게

typedef struct { char line[2][LCD_W + 1]; } LcdFrame;

static QueueHandle_t      s_q_screen = nullptr;     // 앱 → UI. 한 칸, 덮어쓰기
static LiquidCrystal_I2C *s_lcd      = nullptr;
static uint8_t            s_addr     = 0;
static bool               s_lcd_ok   = false;

static LcdFrame s_want;               // 그려야 할 최신 화면
static bool     s_have_want = false;
static LcdFrame s_drawn;              // 지금 LCD 에 떠 있는 글자
static bool     s_drawn_ok[2] = { false, false };   // false 면 그 줄은 무엇이 떠 있는지 모른다 — 다시 쓴다

static uint32_t s_last_draw_ms = 0;
static uint32_t s_retry_at_ms  = 0;
static uint32_t s_hold_until   = 0;

static bool lcd_ping(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static uint8_t lcd_find() {
    for (uint8_t a : LCD_ADDRS) if (lcd_ping(a)) return a;
    return 0;
}

// 찾아서 초기화한다. 라이브러리 초기화에 약 1초가 걸리고, 그동안은 버튼을 읽지 않는다
// (부팅 직후와 LCD 가 다시 연결된 순간에만 일어난다).
static bool lcd_open() {
    const uint8_t addr = lcd_find();
    if (!addr) return false;
    if (!s_lcd || s_addr != addr) {
        delete s_lcd;
        s_lcd  = new LiquidCrystal_I2C(addr, LCD_W, 2);
        s_addr = addr;
    }
    s_lcd->init();
    s_lcd->backlight();
    s_drawn_ok[0] = s_drawn_ok[1] = false;   // 방금 초기화했다 — 화면 내용을 믿지 않는다
    return true;
}

static void lcd_put(uint8_t row, const char *text) {
    s_lcd->setCursor(0, row);
    s_lcd->print(text);
    memcpy(s_drawn.line[row], text, sizeof s_drawn.line[row]);
    s_drawn_ok[row] = true;
}

// 매 틱 한 번. 새 화면을 받아 두고, 그릴 때가 되었으면 바뀐 줄만 다시 쓴다.
static void lcd_service(uint32_t now) {
    LcdFrame f;
    if (xQueueReceive(s_q_screen, &f, 0) == pdTRUE) { s_want = f; s_have_want = true; }

    if (!s_lcd_ok) {
        if ((int32_t)(now - s_retry_at_ms) < 0) return;
        s_lcd_ok = lcd_open();
        s_retry_at_ms = millis() + LCD_RETRY_MS;
        return;
    }
    if ((int32_t)(now - s_hold_until) < 0) return;
    if (!s_have_want) return;
    if (now - s_last_draw_ms < LCD_MIN_INTERVAL_MS) return;

    const bool change0 = !s_drawn_ok[0] || memcmp(s_want.line[0], s_drawn.line[0], LCD_W) != 0;
    const bool change1 = !s_drawn_ok[1] || memcmp(s_want.line[1], s_drawn.line[1], LCD_W) != 0;
    if (!change0 && !change1) return;

    // 쓰기 전에 살아 있는지 짧게 확인한다. 라이브러리는 통신 실패를 알려주지 않으므로,
    // 응답 없는 LCD 에 한 화면을 통째로 쓰면 글자마다 타임아웃을 기다리게 된다.
    if (!lcd_ping(s_addr)) {
        s_lcd_ok = false;
        s_retry_at_ms = now + LCD_RETRY_MS;
        return;
    }
    if (change0) lcd_put(0, s_want.line[0]);
    if (change1) lcd_put(1, s_want.line[1]);
    s_last_draw_ms = now;
}

// ============================================================================
// 태스크
// ============================================================================

void ui_show(const char *line0, const char *line1) {
    if (!s_q_screen) return;
    LcdFrame f;
    snprintf(f.line[0], sizeof f.line[0], "%-16.16s", line0 ? line0 : "");
    snprintf(f.line[1], sizeof f.line[1], "%-16.16s", line1 ? line1 : "");
    xQueueOverwrite(s_q_screen, &f);          // 기다리지 않는다 — 안 그려진 옛 화면은 버려도 된다
}

static void ui_task(void *) {
    // I2C 는 이 태스크에서 연다 — 인터럽트가 이 코어(core 0)에 붙어 센서 코어를 건드리지 않는다.
    Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
    Wire.setTimeOut(I2C_TIMEOUT_MS);

    // 부팅 화면 (목표 1.3). 모터는 setup() 의 motor_init() 이 이미 정지·제동으로 잡아 두었다.
    s_lcd_ok = lcd_open();
    if (s_lcd_ok) {
        lcd_put(0, "Booting...      ");
        lcd_put(1, "Motor: SAFE     ");
        s_hold_until = millis() + LCD_BOOT_HOLD_MS;
    } else {
        s_retry_at_ms = millis() + LCD_RETRY_MS;
    }

    TickType_t next = xTaskGetTickCount();
    for (;;) {
        poll_keys();
        lcd_service(millis());
        vTaskDelayUntil(&next, pdMS_TO_TICKS(KEY_POLL_MS));
    }
}

void ui_start() {
    s_q_screen = xQueueCreate(1, sizeof(LcdFrame));
    for (const auto &k : KEYS) pinMode(k.pin, INPUT_PULLUP);   // 외부 저항 없이 내부 풀업
    key_debounce_init(&s_keys, read_keys(), millis());          // 지금 눌려 있는 버튼은 입력이 아니다
    xTaskCreatePinnedToCore(ui_task, "ui", UI_STACK, nullptr, UI_PRIORITY, nullptr, UI_CORE);
}
