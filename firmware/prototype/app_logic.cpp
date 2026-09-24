#include "app_logic.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board_config.h"

// ---------------------------------------------------------------------------
// 이름표
// ---------------------------------------------------------------------------

const char *screen_name(Screen s) {
    switch (s) {
        case SCR_MODE_SELECT:  return "MODE_SELECT";
        case SCR_NORMAL_SETUP: return "NORMAL_SETUP";
        case SCR_NORMAL_RUN:   return "NORMAL_RUN";
        case SCR_BREATH_SETUP: return "BREATH_SETUP";
        case SCR_SERVICE:      return "SERVICE";
        case SCR_FAULT:        return "FAULT";
        default:               return "?";
    }
}

const char *level_name(Level lv) {
    switch (lv) {
        case LEVEL_LOW:  return "LOW";
        case LEVEL_MID:  return "MID";
        case LEVEL_HIGH: return "HIGH";
        default:         return "?";
    }
}

const char *fault_name(Fault f) {
    switch (f) {
        case FAULT_NONE:           return "none";
        case FAULT_SENSE_STALL:    return "sense_stall";
        case FAULT_SENSE_OVERLOAD: return "sense_overload";
        case FAULT_BAD_STATE:      return "bad_state";
        default:                   return "?";
    }
}

uint8_t level_duty(Level lv) {
    switch (lv) {
        case LEVEL_LOW:  return DUTY_LOW;
        case LEVEL_MID:  return DUTY_MID;
        case LEVEL_HIGH: return DUTY_HIGH;
        default:         return 0;      // 정의되지 않은 강도는 정지 (목표 6.4)
    }
}

// ---------------------------------------------------------------------------
// 상태 전환
// ---------------------------------------------------------------------------

static void go(AppModel *m, Screen s, uint32_t now_ms) {
    m->screen          = s;
    m->cursor          = 0;
    m->screen_since_ms = now_ms;
    if (s != SCR_SERVICE) m->service_duty = 0;
}

static void level_up(AppModel *m)   { if (m->level < LEVEL_HIGH) m->level = (Level)(m->level + 1); }
static void level_down(AppModel *m) { if (m->level > LEVEL_LOW)  m->level = (Level)(m->level - 1); }

void logic_init(AppModel *m, uint32_t now_ms) {
    memset(m, 0, sizeof *m);
    m->level = LEVEL_LOW;
    m->fault = FAULT_NONE;
    go(m, SCR_MODE_SELECT, now_ms);
}

bool logic_is_running(const AppModel *m) {
    return m->screen == SCR_NORMAL_RUN || m->screen == SCR_SERVICE;
}

static bool on_mode_select(AppModel *m, Button b, uint32_t now_ms) {
    if (b == BTN_OK) {
        // 설정 화면에 들어갈 때마다 강도는 LOW 부터다 (목표 3.1.2 / 4.1.3).
        m->level = LEVEL_LOW;
        go(m, m->cursor == 0 ? SCR_NORMAL_SETUP : SCR_BREATH_SETUP, now_ms);
        return true;
    }
    m->cursor ^= 1;                     // 두 항목뿐이라 네 방향 모두 토글
    return true;
}

static bool on_setup(AppModel *m, Button b, uint32_t now_ms) {
    switch (b) {
        case BTN_UP:    m->cursor = (uint8_t)((m->cursor + SETUP_ITEMS - 1) % SETUP_ITEMS); return true;
        case BTN_DOWN:  m->cursor = (uint8_t)((m->cursor + 1) % SETUP_ITEMS);               return true;
        // 화면에는 한 항목만 보이므로, 강도는 POWER 항목이 보일 때만 바꾼다.
        // 보이지 않는 값을 바꾸면 사용자는 무엇이 바뀌었는지 알 수 없다.
        case BTN_LEFT:  if (m->cursor != SETUP_POWER) return false; level_down(m); return true;
        case BTN_RIGHT: if (m->cursor != SETUP_POWER) return false; level_up(m);   return true;
        case BTN_OK:
            if (m->cursor == SETUP_BACK) { go(m, SCR_MODE_SELECT, now_ms); return true; }
            if (m->cursor == SETUP_START) {
                if (m->screen == SCR_NORMAL_SETUP) { go(m, SCR_NORMAL_RUN, now_ms); return true; }
                return false;           // TODO(3단계): 호흡 모드 초기화 → 정착 → 호기 대기 → 실행
            }
            return false;               // POWER 위에서 OK 는 아무 일도 하지 않는다
        default:
            return false;
    }
}

static bool on_normal_run(AppModel *m, Button b, uint32_t now_ms) {
    switch (b) {
        case BTN_UP:
        case BTN_RIGHT: level_up(m);   return true;
        case BTN_DOWN:
        case BTN_LEFT:  level_down(m); return true;
        case BTN_OK:    go(m, SCR_MODE_SELECT, now_ms); return true;   // 타진 정지
        default:        return false;
    }
}

bool logic_button(AppModel *m, Button b, uint32_t now_ms) {
    if (b >= BTN_COUNT) return false;
    if (m->fault != FAULT_NONE || m->screen == SCR_FAULT) return false;

    // 실행 화면의 OK(= 정지)는 잠금을 받지 않는다. 정지는 시작보다 우선이다 (목표 5.6).
    const bool is_stop = logic_is_running(m) && b == BTN_OK;
    if (!is_stop && (uint32_t)(now_ms - m->screen_since_ms) < INPUT_LOCKOUT_MS) return false;

    switch (m->screen) {
        case SCR_MODE_SELECT:  return on_mode_select(m, b, now_ms);
        case SCR_NORMAL_SETUP:
        case SCR_BREATH_SETUP: return on_setup(m, b, now_ms);
        case SCR_NORMAL_RUN:   return on_normal_run(m, b, now_ms);
        case SCR_SERVICE:
            if (b == BTN_OK) { go(m, SCR_MODE_SELECT, now_ms); return true; }
            return false;
        default:
            return false;
    }
}

void logic_stop(AppModel *m, uint32_t now_ms) {
    if (m->screen == SCR_FAULT) return;         // 이미 멈춰 있고, 고장은 풀지 않는다
    if (logic_is_running(m)) go(m, SCR_MODE_SELECT, now_ms);
}

bool logic_service(AppModel *m, uint8_t duty, uint32_t now_ms) {
    if (m->screen != SCR_MODE_SELECT && m->screen != SCR_SERVICE) return false;
    if (duty == 0) {
        if (m->screen == SCR_SERVICE) go(m, SCR_MODE_SELECT, now_ms);
        return true;
    }
    if (m->screen != SCR_SERVICE) go(m, SCR_SERVICE, now_ms);
    m->service_duty = duty;
    return true;
}

void logic_fault(AppModel *m, Fault f, uint32_t now_ms) {
    if (f == FAULT_NONE) return;
    if (m->fault == FAULT_NONE) m->fault = f;   // 첫 원인만 남긴다 — 뒤따르는 것은 대개 그 결과다
    if (m->screen != SCR_FAULT) go(m, SCR_FAULT, now_ms);
}

void logic_tick(AppModel *m, const SenseUpdate *sense, bool backlog, uint32_t now_ms) {
    if (m->fault != FAULT_NONE) return;

    // 상태값 검사 — 여기가 틀리면 어떤 판단도 믿을 수 없다 (목표 8.6).
    if ((unsigned)m->screen >= SCR_COUNT || m->screen == SCR_FAULT ||
        (unsigned)m->level >= LEVEL_COUNT || m->cursor >= SETUP_ITEMS) {
        logic_fault(m, FAULT_BAD_STATE, now_ms);
        return;
    }

    // 샘플을 잃었다 = 앱 태스크가 640ms 넘게 밀렸다. 정상 동작에서는 절대 나오지 않는다.
    if (sense && sense->drops) {
        logic_fault(m, FAULT_SENSE_OVERLOAD, now_ms);
        return;
    }

    // backlog 한두 틱은 흔하다(BLE 연결 수립 등). 1초 넘게 이어지면 과부하다.
    if (backlog) {
        if (m->backlog_ticks < 0xFFFF) m->backlog_ticks++;
        if (m->backlog_ticks >= BACKLOG_FAULT_TICKS) logic_fault(m, FAULT_SENSE_OVERLOAD, now_ms);
    } else {
        m->backlog_ticks = 0;
    }
}

uint8_t logic_output(const AppModel *m, const char **why) {
    const char *reason = "idle";
    uint8_t duty = 0;

    if (m->fault != FAULT_NONE || m->screen == SCR_FAULT) {
        reason = "fault";
    } else if (m->screen == SCR_NORMAL_RUN) {
        duty = level_duty(m->level);            // 범위 밖이면 0 — 다음 틱에 logic_tick 이 FAULT 로 보낸다
        reason = duty ? "run" : "fault";
    } else if (m->screen == SCR_SERVICE) {
        duty = m->service_duty;
        reason = "service";
    }

    if (why) *why = reason;
    return duty;
}

// ---------------------------------------------------------------------------
// 화면 — LCD1602 는 영문만 된다(HD44780 문자 ROM 에 한글이 없다)
// ---------------------------------------------------------------------------

// 16자에 정확히 맞춘다. 짧으면 공백으로 채우고 길면 자른다.
static void put_line(char *dst, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put_line(char *dst, const char *fmt, ...) {
    char tmp[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    size_t n = strlen(tmp);
    if (n > LCD_COLS) n = LCD_COLS;
    memcpy(dst, tmp, n);
    memset(dst + n, ' ', LCD_COLS - n);
    dst[LCD_COLS] = '\0';
}

// 화면 규칙 — 윗줄은 제목(과 항목 위치), 아랫줄은 지금 고른 것 하나.
// 16x2 에 여러 항목을 욱여넣지 않고 한 번에 하나씩 크게 보여준다.
//   "> X"   OK 로 고를 수 있는 항목
//   "< X >" 좌/우로 값을 바꿀 수 있는 항목
//   "n/N"   위/아래로 넘길 항목이 더 있다
void logic_render(const AppModel *m, ScreenLines out) {
    switch (m->screen) {
        case SCR_MODE_SELECT:
            put_line(out[0], "SELECT MODE  %u/2", (unsigned)m->cursor + 1);
            put_line(out[1], "> %s", m->cursor == 0 ? "NORMAL" : "BREATH");
            break;

        case SCR_NORMAL_SETUP:
        case SCR_BREATH_SETUP: {
            const bool normal = m->screen == SCR_NORMAL_SETUP;
            // 호흡 모드는 시작 직전에 벨트 착용을 안내한다 (목표 4.1.2)
            if (!normal && m->cursor == SETUP_START) put_line(out[0], "Wear belt first");
            else put_line(out[0], "%s MODE  %u/%u", normal ? "NORMAL" : "BREATH",
                          (unsigned)m->cursor + 1, (unsigned)SETUP_ITEMS);
            switch (m->cursor) {
                case SETUP_POWER: put_line(out[1], "POWER   < %-4s >", level_name(m->level)); break;
                case SETUP_START: put_line(out[1], "> START");                               break;
                default:          put_line(out[1], "> BACK");                                break;
            }
            break;
        }

        case SCR_NORMAL_RUN:
            put_line(out[0], "NORMAL       RUN");
            put_line(out[1], "POWER:%-4s >STOP", level_name(m->level));
            break;

        case SCR_SERVICE:
            put_line(out[0], "SERVICE MODE");
            put_line(out[1], "DUTY:%-3u   >STOP", (unsigned)m->service_duty);
            break;

        case SCR_FAULT:
        default:
            put_line(out[0], "%s", m->fault == FAULT_SENSE_STALL    ? "SENSOR FAULT"
                                 : m->fault == FAULT_SENSE_OVERLOAD ? "SYSTEM FAULT"
                                                                    : "STATE FAULT");
            put_line(out[1], "POWER OFF & ON");
            break;
    }
}
