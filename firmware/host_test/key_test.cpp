// link_key 디바운서 테스트 — 5ms 마다 읽는 실제 주기로 버튼 파형을 재생한다.
//
// 목표 문서 5장의 버튼 규칙이 하나씩 대응한다: 떨림 무시, 눌린 순간 한 번, 누르고 있어도
// 반복 없음, 뗄 때 입력 없음, 부팅 시 눌려 있던 버튼 무시, 버튼끼리 독립.
//
//   sh firmware/host_test/logic.sh

#include <stdio.h>

#include "link_key.h"

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond) do {                                                          \
    g_checks++;                                                                   \
    if (!(cond)) { g_fail++; printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static const uint8_t OK   = 1u << BTN_OK;
static const uint8_t UP   = 1u << BTN_UP;
static const uint8_t DOWN = 1u << BTN_DOWN;

// raw 를 ms 동안 유지하며 5ms 마다 읽는다. 그동안 나온 입력(비트별 횟수)을 더해 돌려준다.
struct Counts { int ok, up, down; };
static Counts hold(KeyDebouncer *d, uint8_t raw, uint32_t ms, uint32_t *t) {
    Counts c = {0, 0, 0};
    for (uint32_t e = 0; e < ms; e += KEY_POLL_MS) {
        *t += KEY_POLL_MS;
        const uint8_t p = key_debounce(d, raw, *t);
        c.ok += !!(p & OK); c.up += !!(p & UP); c.down += !!(p & DOWN);
    }
    return c;
}

static void test_clean_press() {
    puts("  깨끗하게 누름 → 30ms 뒤 한 번");
    KeyDebouncer d; uint32_t t = 0; key_debounce_init(&d, 0, t);
    CHECK(hold(&d, 0, 100, &t).ok == 0);
    CHECK(hold(&d, OK, KEY_DEBOUNCE_MS - KEY_POLL_MS, &t).ok == 0);   // 아직 안 됨
    CHECK(hold(&d, OK, KEY_POLL_MS * 2, &t).ok == 1);                 // 30ms 채우면 한 번
}

static void test_bounce_ignored() {
    puts("  30ms 보다 짧은 떨림 → 입력 없음");
    KeyDebouncer d; uint32_t t = 0; key_debounce_init(&d, 0, t);
    int n = 0;
    for (int i = 0; i < 20; i++) {           // 10ms 눌림 · 10ms 뗌 을 스무 번
        n += hold(&d, OK, 10, &t).ok;
        n += hold(&d, 0, 10, &t).ok;
    }
    CHECK(n == 0);
    CHECK(hold(&d, 0, 100, &t).ok == 0);
}

static void test_press_with_bounce() {
    puts("  떨리다가 눌림으로 안정 → 한 번");
    KeyDebouncer d; uint32_t t = 0; key_debounce_init(&d, 0, t);
    int n = 0;
    for (int i = 0; i < 4; i++) { n += hold(&d, OK, 5, &t).ok; n += hold(&d, 0, 5, &t).ok; }
    n += hold(&d, OK, 200, &t).ok;
    CHECK(n == 1);
}

static void test_hold_no_repeat() {
    puts("  누르고 5초 유지 → 한 번만 (자동 반복 없음)");
    KeyDebouncer d; uint32_t t = 0; key_debounce_init(&d, 0, t);
    CHECK(hold(&d, OK, 5000, &t).ok == 1);
}

static void test_release_bounce() {
    puts("  뗄 때 떨림 → 새 입력 없음");
    KeyDebouncer d; uint32_t t = 0; key_debounce_init(&d, 0, t);
    CHECK(hold(&d, OK, 200, &t).ok == 1);
    int n = 0;
    for (int i = 0; i < 5; i++) { n += hold(&d, 0, 10, &t).ok; n += hold(&d, OK, 10, &t).ok; }
    n += hold(&d, 0, 200, &t).ok;
    CHECK(n == 0);
}

static void test_press_again() {
    puts("  떼었다 다시 누름 → 두 번째 입력");
    KeyDebouncer d; uint32_t t = 0; key_debounce_init(&d, 0, t);
    CHECK(hold(&d, OK, 100, &t).ok == 1);
    CHECK(hold(&d, 0, 100, &t).ok == 0);
    CHECK(hold(&d, OK, 100, &t).ok == 1);
}

static void test_held_at_boot() {
    puts("  부팅할 때 눌려 있음 → 입력 없음, 떼었다 누르면 입력");
    KeyDebouncer d; uint32_t t = 0; key_debounce_init(&d, OK, t);
    CHECK(hold(&d, OK, 3000, &t).ok == 0);
    CHECK(hold(&d, 0, 100, &t).ok == 0);
    CHECK(hold(&d, OK, 100, &t).ok == 1);
}

static void test_independent_keys() {
    puts("  버튼끼리 독립 — 동시 누름은 각각 한 번, 한 버튼의 떨림이 다른 버튼을 늦추지 않음");
    KeyDebouncer d; uint32_t t = 0; key_debounce_init(&d, 0, t);
    Counts c = hold(&d, OK | UP, 100, &t);
    CHECK(c.ok == 1); CHECK(c.up == 1); CHECK(c.down == 0);
    hold(&d, 0, 100, &t);

    // UP 이 계속 떨리는 동안 DOWN 을 깨끗하게 누르면 DOWN 은 제때(30ms) 들어온다
    int down_at = -1, ups = 0;
    for (int ms = 0; ms < 100; ms += KEY_POLL_MS) {
        const uint8_t up_bounce = ((ms / 5) % 2) ? UP : 0;
        t += KEY_POLL_MS;
        const uint8_t p = key_debounce(&d, (uint8_t)(DOWN | up_bounce), t);
        if ((p & DOWN) && down_at < 0) down_at = ms;
        ups += !!(p & UP);
    }
    CHECK(down_at >= 0 && down_at <= KEY_DEBOUNCE_MS + KEY_POLL_MS);
    CHECK(ups == 0);
}

static void test_time_wrap() {
    puts("  millis() 가 넘어가는 순간에도 정상");
    KeyDebouncer d; uint32_t t = 0xFFFFFFFFu - 12; key_debounce_init(&d, 0, t);
    CHECK(hold(&d, OK, 100, &t).ok == 1);
    CHECK(hold(&d, OK, 1000, &t).ok == 0);
}

int main() {
    puts("link_key (디바운서)");
    test_clean_press();
    test_bounce_ignored();
    test_press_with_bounce();
    test_hold_no_repeat();
    test_release_bounce();
    test_press_again();
    test_held_at_boot();
    test_independent_keys();
    test_time_wrap();
    printf(g_fail ? "→ %d/%d 실패\n" : "→ %d개 검사 통과\n", g_fail ? g_fail : g_checks, g_checks);
    return g_fail ? 1 : 0;
}
