// 5방향 스위치 디바운서 — 순수 로직.
//
// GPIO 를 읽지 않는다. 읽은 값(눌린 버튼 비트마스크)과 시각을 받아 "이번에 새로 눌린 버튼" 만
// 돌려준다. GPIO 읽기와 태스크는 task_ui.cpp 가 맡는다. 덕분에 호스트에서 떨림·누르고 있기·
// 동시 누름을 그대로 재생해 검증할 수 있다(host_test/key_test.cpp).
//
// --- 규칙 (목표 문서 5장) ---
//  1. 원시 값이 KEY_DEBOUNCE_MS 동안 그대로여야 인정한다. 그보다 짧은 떨림은 없던 일이다.
//  2. 떼어 있다가 눌린 순간 한 번만 알린다. 누르고 있어도, 뗄 때도 알리지 않는다(자동 반복 없음).
//  3. 버튼마다 따로 센다 — 한 버튼이 떨려도 다른 버튼의 판정이 늦어지지 않는다.
//  4. 시작할 때 이미 눌려 있던 버튼은 "눌림" 으로 시작한다. 떼었다 다시 눌러야 입력이 된다.
//     전원을 켜는 동안 누르고 있거나 버튼이 끼어 있어도 입력이 생기지 않는다.

#ifndef LINK_KEY_H
#define LINK_KEY_H

#include <stdint.h>

#include "app_types.h"   // Button, BTN_COUNT — 비트 번호 = Button 값

#define KEY_DEBOUNCE_MS  30   // 목표 문서 5.1 의 30~50ms 중 가장 빠른 쪽
#define KEY_POLL_MS       5   // 이 주기로 읽는다. 디바운스 한 번에 여섯 번 본다

typedef struct {
    uint8_t  stable;                 // 인정된 상태. 비트 1 = 눌림
    uint8_t  last_raw;               // 직전에 읽은 원시 값
    uint32_t since_ms[BTN_COUNT];    // 버튼마다 원시 값이 마지막으로 바뀐 시각
} KeyDebouncer;

// 지금 읽은 값을 그대로 인정된 상태로 삼는다(규칙 4).
void key_debounce_init(KeyDebouncer *d, uint8_t raw_pressed, uint32_t now_ms);

// 읽을 때마다 부른다. 이번에 새로 눌린 버튼의 비트마스크를 돌려준다(없으면 0).
uint8_t key_debounce(KeyDebouncer *d, uint8_t raw_pressed, uint32_t now_ms);

#endif  // LINK_KEY_H
