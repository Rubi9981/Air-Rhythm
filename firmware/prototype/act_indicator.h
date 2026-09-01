// 온보드 RGB LED — 시스템 상태를 눈으로 보는 창.
//
// 모터 하드웨어가 오기 전까지 "BLE 명령이 실제로 도착했는가" 를 확인하는 수단이지만,
// 버리는 테스트 코드가 아니다. 모터가 붙은 뒤에도 진단 표시로 계속 쓴다.
// 그래서 act_motor 와 같은 자리(중재 뒤의 출력 계층)에 두고 같은 규칙을 따른다.
//
// --- 지켜야 할 것 ---
//  1. app_task(core 0)에서만 부른다. rgbLedWrite() 는 rmtInit 을 매번 다시 하고
//     RMT_WAIT_FOR_EVER 로 전송 완료를 기다리므로 100~300µs 블로킹이다.
//     sense_task(core 1)에서 부르면 20ms 주기가 깨진다.
//  2. 매 틱 indicator_show() 를 부르되, 실제 쓰기는 색이 바뀔 때만 일어난다.
//     WS2812 는 PWM 처럼 상태를 유지하므로 같은 색을 다시 쓸 이유가 없다.
//  3. 레벨 트리거 — 갱신이 끊기면 꺼진다. 모터와 같은 안전 성질을 갖게 한다.

#ifndef ACT_INDICATOR_H
#define ACT_INDICATOR_H

#include <stdint.h>

// 표시할 상태. app_task 가 매 틱 채워서 넘긴다.
// 색을 고르는 정책은 전부 act_indicator.cpp 안에 있다 — 호출부는 사실만 전달한다.
typedef struct {
    uint8_t duty;         // 모터 세기 0~255. 0 이면 정지
    bool    ble_linked;   // BLE 연결됨
    bool    settled;      // 정착이 끝나 판정이 유효하다
    bool    signal_ok;    // 스트랩이 붙어 있다
    bool    fault;        // 센서 정지 등 결함
} IndicatorState;

// 한 번만 반짝이고 지나가는 신호. 정상 색 위에 잠깐 덮어쓴다.
typedef enum {
    FLASH_NONE = 0,
    FLASH_CMD_OK,    // 명령을 받아 반영했다 (흰색)
    FLASH_CMD_ERR,   // 명령을 거부했다 (빨강)
} IndicatorFlash;

void indicator_init();

// 매 틱 호출. 색이 바뀔 때만 실제로 LED 에 쓴다.
void indicator_show(const IndicatorState *state);

// 명령이 도착한 순간 한 번 호출. 다음 몇 틱 동안 indicator_show 를 덮어쓴다.
// "BLE 신호가 실제로 들어왔는가" 를 명령의 성패와 무관하게 눈으로 확인하는 통로다.
void indicator_flash(IndicatorFlash kind);

#endif  // ACT_INDICATOR_H
