#include "app_logic.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "breath_slope.h"   // BR_RISING(흡기) / BR_FALLING(호기) / BR_UNKNOWN

// ---------------------------------------------------------------------------
// 이름표
// ---------------------------------------------------------------------------

const char *screen_name(Screen s) {
    switch (s) {
        case SCR_MODE_SELECT:  return "MODE_SELECT";
        case SCR_NORMAL_SETUP: return "NORMAL_SETUP";
        case SCR_NORMAL_RUN:   return "NORMAL_RUN";
        case SCR_BREATH_SETUP: return "BREATH_SETUP";
        case SCR_BREATH_INIT:  return "BREATH_INIT";
        case SCR_BREATH_WAIT:  return "BREATH_WAIT";
        case SCR_BREATH_RUN:   return "BREATH_RUN";
        case SCR_NO_SIGNAL:    return "NO_SIGNAL";
        case SCR_SIGNAL_LOST:  return "SIGNAL_LOST";
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
    if (s != SCR_SERVICE)     m->service_duty = 0;
    if (s != SCR_BREATH_INIT) m->reset_wanted = false;   // 요청하기 전에 떠났으면 요청도 거둔다
}

static void level_up(AppModel *m)   { if (m->level < LEVEL_HIGH) m->level = (Level)(m->level + 1); }
static void level_down(AppModel *m) { if (m->level > LEVEL_LOW)  m->level = (Level)(m->level - 1); }

// 호흡 모드 시작(또는 RETRY). 모터는 이 화면들에서 출력 0 이고, 검출기를 처음부터 다시
// 정착시킨다 (목표 4.2.1~3). 이전 회차의 위상·호흡수는 화면에서 지운다.
static void start_breath(AppModel *m, uint32_t now_ms) {
    go(m, SCR_BREATH_INIT, now_ms);
    m->reset_wanted = true;
    m->phase        = BR_UNKNOWN;
    m->bpm          = 0.0f;
}

void logic_init(AppModel *m, uint32_t now_ms) {
    memset(m, 0, sizeof *m);
    m->level = LEVEL_LOW;
    m->fault = FAULT_NONE;
    m->phase = BR_UNKNOWN;
    go(m, SCR_MODE_SELECT, now_ms);
}

bool logic_is_running(const AppModel *m) {
    return m->screen == SCR_NORMAL_RUN || m->screen == SCR_BREATH_RUN || m->screen == SCR_SERVICE;
}

bool logic_in_breath_flow(const AppModel *m) {
    switch (m->screen) {
        case SCR_BREATH_INIT:
        case SCR_BREATH_WAIT:
        case SCR_BREATH_RUN:
        case SCR_NO_SIGNAL:
        case SCR_SIGNAL_LOST: return true;
        default:              return false;
    }
}

// OK 가 "멈춰라 / 그만둬라" 인 화면. 이 화면들의 OK 는 입력 잠금을 받지 않는다.
static bool ok_means_stop(const AppModel *m) {
    return logic_is_running(m) || m->screen == SCR_BREATH_INIT || m->screen == SCR_BREATH_WAIT;
}

static bool on_mode_select(AppModel *m, Button b, uint32_t now_ms) {
    if (b == BTN_OK) {
        // 설정 화면에 들어갈 때마다 강도는 LOW 부터다 (목표 3.1.2 / 4.1.3).
        m->level = LEVEL_LOW;
        go(m, m->cursor == MODE_NORMAL ? SCR_NORMAL_SETUP : SCR_BREATH_SETUP, now_ms);
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
                if (m->screen == SCR_NORMAL_SETUP) go(m, SCR_NORMAL_RUN, now_ms);
                else                               start_breath(m, now_ms);
                return true;
            }
            return false;               // POWER 위에서 OK 는 아무 일도 하지 않는다
        default:
            return false;
    }
}

// 일반·호흡 실행 화면 공통 — 네 방향으로 강도 변경, OK 는 정지 (목표 3.2.3 / 4.4.4)
static bool on_run(AppModel *m, Button b, uint32_t now_ms) {
    switch (b) {
        case BTN_UP:
        case BTN_RIGHT: level_up(m);   return true;
        case BTN_DOWN:
        case BTN_LEFT:  level_down(m); return true;
        case BTN_OK:    go(m, SCR_MODE_SELECT, now_ms); return true;   // 타진 정지
        default:        return false;
    }
}

// NO_SIGNAL / SIGNAL_LOST — RETRY 는 12초 초기화부터 다시, BACK 은 모드 선택 (목표 4.3.6 / 4.5.4)
static bool on_choice(AppModel *m, Button b, uint32_t now_ms) {
    if (b != BTN_OK) { m->cursor ^= 1; return true; }
    if (m->cursor == CHOICE_RETRY) start_breath(m, now_ms);
    else                           go(m, SCR_MODE_SELECT, now_ms);
    return true;
}

bool logic_button(AppModel *m, Button b, uint32_t now_ms) {
    if (b >= BTN_COUNT) return false;
    if (m->fault != FAULT_NONE || m->screen == SCR_FAULT) return false;

    // 멈추는 OK 는 잠금을 받지 않는다. 정지는 시작보다 우선이다 (목표 5.6).
    const bool is_stop = ok_means_stop(m) && b == BTN_OK;
    if (!is_stop && (uint32_t)(now_ms - m->screen_since_ms) < INPUT_LOCKOUT_MS) return false;

    switch (m->screen) {
        case SCR_MODE_SELECT:  return on_mode_select(m, b, now_ms);
        case SCR_NORMAL_SETUP:
        case SCR_BREATH_SETUP: return on_setup(m, b, now_ms);
        case SCR_NORMAL_RUN:
        case SCR_BREATH_RUN:   return on_run(m, b, now_ms);
        case SCR_BREATH_INIT:
        case SCR_BREATH_WAIT:
            // 초기화·대기 중에는 OK(취소)만 받는다. 모터는 이미 멈춰 있다 (목표 4.2.9).
            if (b == BTN_OK) { go(m, SCR_MODE_SELECT, now_ms); return true; }
            return false;
        case SCR_NO_SIGNAL:
        case SCR_SIGNAL_LOST:  return on_choice(m, b, now_ms);
        case SCR_SERVICE:
            if (b == BTN_OK) { go(m, SCR_MODE_SELECT, now_ms); return true; }
            return false;
        default:
            return false;
    }
}

void logic_stop(AppModel *m, uint32_t now_ms) {
    if (m->screen == SCR_FAULT) return;         // 이미 멈춰 있고, 고장은 풀지 않는다
    if (ok_means_stop(m)) go(m, SCR_MODE_SELECT, now_ms);
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

bool logic_reset_wanted(const AppModel *m) {
    return m->reset_wanted;
}

void logic_reset_issued(AppModel *m, uint16_t epoch) {
    m->sense_epoch  = epoch;
    m->reset_wanted = false;
}

// 이 샘플이 요청한 초기화 이후의 것인가. 요청 전이거나 이전 회차면 그 플래그·위상은 옛 검출기의 것이다.
static bool is_current(const AppModel *m, const SenseUpdate *s) {
    return !m->reset_wanted && s->epoch == m->sense_epoch;
}

static bool signal_bad(const SenseUpdate *s) {
    return (s->events & EVENT_SIGNAL_LOST) || !(s->flags & FLAG_SIGNAL_OK);
}

// 호흡 모드 전환 — 이벤트가 아니라 샘플마다 다시 판단하는 것은 출력(logic_output) 쪽이고,
// 여기는 "화면을 넘길 때" 만 이벤트를 쓴다. 호기 시작처럼 "다음 것" 을 기다려야 하는 판단은
// 위상만으로는 할 수 없기 때문이다 (목표 4.3.3~4).
static void breath_tick(AppModel *m, const SenseUpdate *s, uint32_t now_ms) {
    if (!s || !logic_in_breath_flow(m)) return;

    const bool current = is_current(m, s);
    if (current) {                               // 표시용 — 초기화 전 샘플은 화면에 올리지 않는다
        m->phase = s->phase;
        m->bpm   = s->bpm;
    }
    const uint32_t in_screen = now_ms - m->screen_since_ms;

    switch (m->screen) {
        case SCR_BREATH_INIT:
            // 12초 카운트다운이 아니라 검출기의 "정착 완료" 로만 넘어간다 (목표 4.2.6~7).
            if (current && (s->flags & FLAG_SETTLED)) {
                go(m, SCR_BREATH_WAIT, now_ms);
                return;                          // 이 샘플의 이벤트로 곧바로 시작하지 않는다
            }
            if (in_screen >= BREATH_INIT_TIMEOUT_MS) go(m, SCR_NO_SIGNAL, now_ms);
            return;

        case SCR_BREATH_WAIT:
            if (!current) return;
            // 정착 직후의 SIGNAL_OK 는 검출기가 1 로 두는 기본값이라 호흡의 증거가 아니다.
            // 그래서 "시간 안에 호기 시작을 실제로 보았는가" 로 판정한다.
            if (signal_bad(s))                        { go(m, SCR_NO_SIGNAL, now_ms);  return; }
            if (s->events & EVENT_EXHALE)             { go(m, SCR_BREATH_RUN, now_ms); return; }
            if (in_screen >= BREATH_WAIT_TIMEOUT_MS)  { go(m, SCR_NO_SIGNAL, now_ms);  return; }
            return;

        case SCR_BREATH_RUN:
            // 신호를 잃으면 멈추고 그 화면에 머문다. 돌아와도 스스로 다시 시작하지 않는다 (목표 4.5).
            if (current && signal_bad(s)) go(m, SCR_SIGNAL_LOST, now_ms);
            return;

        default:
            return;
    }
}

// 화면마다 커서가 가질 수 있는 항목 수. 선택 항목이 없는 화면은 커서가 늘 0 이다.
static uint8_t cursor_items(Screen s) {
    switch (s) {
        case SCR_MODE_SELECT:  return MODE_ITEMS;
        case SCR_NORMAL_SETUP:
        case SCR_BREATH_SETUP: return SETUP_ITEMS;
        case SCR_NO_SIGNAL:
        case SCR_SIGNAL_LOST:  return CHOICE_ITEMS;
        default:               return 1;
    }
}

void logic_tick(AppModel *m, const SenseUpdate *sense, bool backlog, uint32_t now_ms) {
    if (m->fault != FAULT_NONE) return;

    // 상태값 검사 — 여기가 틀리면 어떤 판단도 믿을 수 없다 (목표 8.6).
    // 커서는 화면마다 범위가 다르다. 두 항목 화면의 커서 2 도 오염으로 본다.
    if ((unsigned)m->screen >= SCR_COUNT || m->screen == SCR_FAULT ||
        (unsigned)m->level >= LEVEL_COUNT || m->cursor >= cursor_items(m->screen)) {
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
        if (m->backlog_ticks >= BACKLOG_FAULT_TICKS) {
            logic_fault(m, FAULT_SENSE_OVERLOAD, now_ms);
            return;
        }
    } else {
        m->backlog_ticks = 0;
    }

    breath_tick(m, sense, now_ms);
}

// 호흡 실행 중 이번 샘플로 때려도 되는가. 매 틱 처음부터 다시 판단한다 — 이전 틱의 결정을
// 물려받지 않으므로 어긋난 상태가 남을 수 없다.
static uint8_t breath_output(const AppModel *m, const SenseUpdate *s, bool backlog, const char **why) {
    // 큐에 밀린 것이 있다 = 지금 든 sense 가 최신이 아니다. q_sense 는 32칸이라 최악의 경우
    // 640ms 묵은 위상이고, 호흡 한 주기가 3~4초이므로 흡기·호기가 뒤집히기에 충분하다.
    if (!s || backlog)                  { *why = "backlog";  return 0; }
    if (!is_current(m, s))              { *why = "reset";    return 0; }
    if (!(s->flags & FLAG_SETTLED))     { *why = "settling"; return 0; }
    if (!(s->flags & FLAG_SIGNAL_OK))   { *why = "nosig";    return 0; }
    // 흡기 중에는 절대 때리지 않는다. 이 프로젝트의 핵심 요구사항이다.
    if (s->phase != BR_FALLING)         { *why = "inhale";   return 0; }

    const uint8_t duty = level_duty(m->level);
    *why = duty ? "run" : "fault";
    return duty;
}

uint8_t logic_output(const AppModel *m, const SenseUpdate *sense, bool backlog, const char **why) {
    const char *reason = "idle";
    uint8_t duty = 0;

    if (m->fault != FAULT_NONE || m->screen == SCR_FAULT) {
        reason = "fault";
    } else {
        switch (m->screen) {
            case SCR_NORMAL_RUN:
                // 일반 모드는 호흡과 무관하다 — backlog·위상을 보지 않는다.
                duty = level_duty(m->level);      // 범위 밖이면 0 — 다음 틱에 logic_tick 이 FAULT 로 보낸다
                reason = duty ? "run" : "fault";
                break;
            case SCR_BREATH_RUN:  duty = breath_output(m, sense, backlog, &reason); break;
            case SCR_SERVICE:     duty = m->service_duty; reason = "service";        break;
            case SCR_BREATH_INIT: reason = "init"; break;
            case SCR_BREATH_WAIT: reason = "wait"; break;
            default:              break;
        }
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

static char mark(const AppModel *m, uint8_t item) { return m->cursor == item ? '>' : ' '; }

// 화면 규칙 — 윗줄은 제목(과 항목 위치), 아랫줄은 지금 고른 것 하나.
// 16x2 에 여러 항목을 욱여넣지 않고 한 번에 하나씩 크게 보여준다.
//   "> X"   OK 로 고를 수 있는 항목
//   "< X >" 좌/우로 값을 바꿀 수 있는 항목
//   "n/N"   위/아래로 넘길 항목이 더 있다
void logic_render(const AppModel *m, ScreenLines out) {
    switch (m->screen) {
        case SCR_MODE_SELECT:
            put_line(out[0], "SELECT MODE  %u/%u", (unsigned)m->cursor + 1, (unsigned)MODE_ITEMS);
            put_line(out[1], "> %s", m->cursor == MODE_NORMAL ? "NORMAL" : "BREATH");
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

        case SCR_BREATH_INIT:
            put_line(out[0], "Detecting breath");
            put_line(out[1], "Please Wait...");
            break;

        case SCR_BREATH_WAIT:
            put_line(out[0], "Breath detected");
            put_line(out[1], "Wait for EXHALE");
            break;

        case SCR_BREATH_RUN: {
            // 윗줄: 동기 상태 + 위상 + 호흡수 / 아랫줄: 강도 + 모터 상태 + 정지 (목표 4.4.3)
            char bpm[8];
            if (m->bpm > 0.0f) snprintf(bpm, sizeof bpm, "%.1f", (double)m->bpm);
            else               snprintf(bpm, sizeof bpm, "--.-");
            const char *ph = m->phase == BR_FALLING ? "EXH" : m->phase == BR_RISING ? "INH" : "---";
            put_line(out[0], "SYNC %-3s %7s", ph, bpm);
            put_line(out[1], "%-4s %-4s  >STOP", level_name(m->level),
                     m->phase == BR_FALLING ? "RUN" : "WAIT");
            break;
        }

        case SCR_NO_SIGNAL:
        case SCR_SIGNAL_LOST:
            put_line(out[0], "%s", m->screen == SCR_NO_SIGNAL ? "NO BREATH SIGNAL" : "SIGNAL LOST");
            put_line(out[1], "%cRETRY     %cBACK", mark(m, CHOICE_RETRY), mark(m, CHOICE_BACK));
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
