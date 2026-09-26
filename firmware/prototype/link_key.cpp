#include "link_key.h"

void key_debounce_init(KeyDebouncer *d, uint8_t raw_pressed, uint32_t now_ms) {
    d->stable   = raw_pressed;
    d->last_raw = raw_pressed;
    for (int k = 0; k < BTN_COUNT; k++) d->since_ms[k] = now_ms;
}

uint8_t key_debounce(KeyDebouncer *d, uint8_t raw_pressed, uint32_t now_ms) {
    uint8_t pressed = 0;

    for (int k = 0; k < BTN_COUNT; k++) {
        const uint8_t bit = (uint8_t)(1u << k);
        const uint8_t raw = raw_pressed & bit;

        if (raw != (d->last_raw & bit)) {
            d->since_ms[k] = now_ms;                // 원시 값이 바뀌었다 — 처음부터 다시 센다
        } else if (raw != (d->stable & bit) &&
                   (uint32_t)(now_ms - d->since_ms[k]) >= KEY_DEBOUNCE_MS) {
            d->stable ^= bit;                       // 충분히 오래 그대로였다 — 인정한다
            if (raw) pressed |= bit;                // 눌린 쪽으로 바뀐 것만 알린다
        }
    }

    d->last_raw = raw_pressed;
    return pressed;
}
