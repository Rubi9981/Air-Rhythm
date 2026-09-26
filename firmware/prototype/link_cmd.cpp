/**
 * ============================================================================
 * [link_cmd.cpp] 명령 파서 구현체 (시리얼 텍스트)
 * ============================================================================
 *
 * 역할:
 *   - 시리얼 모니터로부터 들어온 텍스트 명령 파싱 (cmd_parse)
 *   - 유효성이 검증된 Command 구조체를 FreeRTOS 큐(q_cmd)에 안전하게 삽입
 *
 * BLE 는 모니터링 전용이라 앱에서 오는 명령을 파싱하지 않는다.
 * ============================================================================
 */

#include "link_cmd.h"

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "link_msg.h"

// ============================================================================
// [1. 시리얼 텍스트 명령 파서] - 순수 함수
// ============================================================================

static const char *skip_space(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

// 대소문자를 무시하고 접두어를 맞춰 본다. 맞으면 그 뒤를 가리키는 포인터를,
// 아니면 nullptr 을 돌려준다.
static const char *match_ci(const char *s, const char *prefix) {
    while (*prefix) {
        if (toupper((unsigned char)*s) != toupper((unsigned char)*prefix)) return nullptr;
        s++; prefix++;
    }
    return s;
}

// 남은 글자가 공백뿐인지. "STOPX" 를 "STOP" 으로 잘못 읽지 않게 한다.
static bool only_space(const char *s) {
    return *skip_space(s) == '\0';
}

// 인자 없는 명령어 하나와 정확히 일치하는지.
static bool is_word(const char *p, const char *word) {
    const char *rest = match_ci(p, word);
    return rest != nullptr && only_space(rest);
}

static bool set_cmd(Command *out, CmdType type, int32_t arg) {
    out->type = type;
    out->arg  = arg;
    return true;
}

// "BTN" 뒤의 버튼 이름. 한 글자 약칭과 전체 이름을 모두 받는다.
static bool parse_button(const char *p, Button *b) {
    static const struct { const char *name; Button b; } table[] = {
        { "U", BTN_UP },    { "UP", BTN_UP },
        { "D", BTN_DOWN },  { "DOWN", BTN_DOWN },
        { "L", BTN_LEFT },  { "LEFT", BTN_LEFT },
        { "R", BTN_RIGHT }, { "RIGHT", BTN_RIGHT },
        { "OK", BTN_OK },
    };
    for (const auto &e : table) {
        if (is_word(p, e.name)) { *b = e.b; return true; }
    }
    return false;
}

bool cmd_parse(const char *line, Command *out) {
    if (!line || !out) return false;

    const char *p = skip_space(line);
    const char *rest;

    // "BTN OK" — 물리 버튼과 똑같은 경로로 들어간다
    if ((rest = match_ci(p, "BTN")) != nullptr) {
        Button b;
        if (!parse_button(skip_space(rest), &b)) return false;
        return set_cmd(out, CMD_BUTTON, (int32_t)b);
    }

    // "BREATH RESET" — 검출기 재정착(디버그용. 이후 호흡 모드 진입 흐름이 같은 명령을 쓴다)
    if ((rest = match_ci(p, "BREATH")) != nullptr) {
        rest = skip_space(rest);
        if (is_word(rest, "RESET")) return set_cmd(out, CMD_BREATH_RESET, 0);
        return false;
    }

    // "DUTY 200" — 서비스 모드 전용. 값 검증까지 여기서 끝낸다. 경계에서 거르므로 app_task 는
    // "이미 유효한 명령"만 다루면 되고, 잘못된 값이 시스템 안으로 들어오는 길이 없다.
    if ((rest = match_ci(p, "DUTY")) != nullptr) {
        rest = skip_space(rest);
        if (!*rest) return false;
        char *end;
        long v = strtol(rest, &end, 10);
        if (end == rest || !only_space(end)) return false;   // 숫자가 아니거나 뒤에 군더더기
        if (v < 0 || v > 255) return false;
        return set_cmd(out, CMD_SET_DUTY, (int32_t)v);
    }

    if (is_word(p, "STOP"))     return set_cmd(out, CMD_STOP, 0);
    if (is_word(p, "ESTOP"))    return set_cmd(out, CMD_STOP, 0);
    if (is_word(p, "STATUS"))   return set_cmd(out, CMD_STATUS, 0);
    if (is_word(p, "SELFTEST")) return set_cmd(out, CMD_SELFTEST, 0);

    return false;   // out->src 는 호출자가 채운다. 실패 시 *out 은 건드리지 않았다
}

// ============================================================================
// [2. 유틸리티 및 큐 삽입]
// ============================================================================

const char *cmd_name(CmdType t) {
    switch (t) {
        case CMD_BUTTON:            return "BTN";
        case CMD_STOP:              return "STOP";
        case CMD_SET_DUTY:          return "DUTY";
        case CMD_STATUS:            return "STATUS";
        case CMD_SELFTEST:          return "SELFTEST";
        case CMD_BREATH_RESET:      return "BREATH RESET";
        case CMD_BLE_CONNECTED:     return "BLE_CONNECTED";
        default:                    return "?";
    }
}

const char *button_name(Button b) {
    switch (b) {
        case BTN_UP:    return "UP";
        case BTN_DOWN:  return "DOWN";
        case BTN_LEFT:  return "LEFT";
        case BTN_RIGHT: return "RIGHT";
        case BTN_OK:    return "OK";
        default:        return "?";
    }
}

bool cmd_submit(const Command *cmd) {
    if (!q_cmd || cmd == nullptr) return false;

    // 대기 0. BLE 콜백 문맥에서 불릴 수 있으므로 절대 블로킹하면 안 된다.
    return xQueueSend(q_cmd, cmd, 0) == pdTRUE;
}

// 시리얼 입력 — 버튼·LCD 없이 전체 명령 경로를 시험하는 통로
void cmd_poll_serial() {
    static char   buf[64];
    static size_t len = 0;

    // 도착한 만큼만 읽는다. 여기서 블로킹하면 app_task 가 멈추고,
    // 그러면 레벨 트리거인 motor_set() 갱신도 끊긴다.
    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == '\n' || c == '\r') {
            if (len == 0) continue;              // 빈 줄(CRLF 의 두 번째 글자 등)은 무시
            buf[len] = '\0';
            len = 0;

            Command cmd = {};
            if (cmd_parse(buf, &cmd)) {
                cmd.src = SRC_SERIAL;
                cmd_submit(&cmd);
            } else {
                msg_ack_err(SRC_SERIAL, "unknown");
            }
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = c;
        } else {
            len = 0;                             // 너무 긴 줄은 통째로 버린다
        }
    }
}
