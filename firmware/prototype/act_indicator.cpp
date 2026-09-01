#include "act_indicator.h"

#include <Arduino.h>

#include "board_config.h"

// 마지막으로 LED 에 실제로 쓴 색. 같은 색이면 다시 쓰지 않는다(헤더 주석 2번).
static uint8_t last_r = 0xFF, last_g = 0xFF, last_b = 0xFF;   // 첫 호출은 반드시 쓰이도록

static IndicatorFlash flash_kind  = FLASH_NONE;
static uint8_t        flash_ticks = 0;

// 실제 쓰기는 여기 한 곳에서만. 변화가 없으면 조용히 빠져나간다.
static void write_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (r == last_r && g == last_g && b == last_b) return;
    last_r = r; last_g = g; last_b = b;
    rgbLedWrite(RGB_PIN, r, g, b);
}

void indicator_init() {
    last_r = last_g = last_b = 0xFF;   // 강제로 한 번 쓰이게
    write_rgb(0, 0, 0);
}

void indicator_flash(IndicatorFlash kind) {
    flash_kind  = kind;
    flash_ticks = FLASH_TICKS;
}

void indicator_show(const IndicatorState *state) {
    // ① 플래시가 남아 있으면 그것이 우선. 명령 도착을 놓치지 않기 위해서다.
    if (flash_ticks) {
        flash_ticks--;
        // TODO(구현): FLASH_CMD_OK → 흰색, FLASH_CMD_ERR → 빨강.
        //   밝기는 RGB_MAX_LEVEL 로 제한할 것 (온보드 LED 가 매우 밝다).
        return;
    }
    flash_kind = FLASH_NONE;

    // ② 정상 표시. 위에서부터 먼저 맞는 것 하나만 쓴다 — 우선순위가 곧 중요도다.
    // TODO(구현): 아래 표대로 색을 정해 write_rgb() 를 부를 것.
    //
    //   조건                     색           뜻
    //   ─────────────────────────────────────────────────────────────
    //   state->fault             빨강 깜빡    센서 정지 — 최우선
    //   state->duty > 0          초록(밝기∝duty) 모터 동작 중 (호기)
    //   !state->signal_ok        노랑 약하게  스트랩 빠짐
    //   !state->settled          파랑 숨쉬듯  정착 중 (12초)
    //   state->ble_linked        파랑 약하게  연결됨, 대기
    //   그 외                    꺼짐         미연결, 대기
    //
    // 깜빡임·숨쉬기는 이 함수가 20ms 마다 불린다는 점을 이용해 틱을 세면 된다.
    //   static uint16_t tick; tick++;
    //   빨강 깜빡: (tick / 10) % 2   → 200ms 주기
    (void)state;
    write_rgb(0, 0, 0);
}
