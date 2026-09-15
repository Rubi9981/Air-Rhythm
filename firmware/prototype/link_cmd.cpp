#include "link_cmd.h"

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "link_msg.h"

// ---------------------------------------------------------------------------
// 파싱 — 순수 함수. Serial 도 BLE 도 여기로 들어온다.
// ---------------------------------------------------------------------------

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

// 남은 글자가 공백뿐인지. "MOTOR ONX" 를 "MOTOR ON" 으로 잘못 읽지 않게 한다.
static bool only_space(const char *s) {
    return *skip_space(s) == '\0';
}

bool cmd_parse(const char *line, Command *out) {
    if (!line || !out) return false;

    const char *p = skip_space(line);
    const char *rest;

    // "MOTOR ON" / "MOTOR OFF"
    if ((rest = match_ci(p, "MOTOR")) != nullptr) {
        rest = skip_space(rest);
        const char *tail;
        if ((tail = match_ci(rest, "ON")) != nullptr && only_space(tail)) {
            out->type = CMD_MOTOR_ON;
            out->arg  = 0;
            return true;
        }
        if ((tail = match_ci(rest, "OFF")) != nullptr && only_space(tail)) {
            out->type = CMD_MOTOR_OFF;
            out->arg  = 0;
            return true;
        }
        return false;
    }

    // "DUTY 200" — 값 검증까지 여기서 끝낸다. 경계에서 거르므로 app_task 는
    // "이미 유효한 명령"만 다루면 되고, 잘못된 값이 시스템 안으로 들어오는 길이 없다.
    if ((rest = match_ci(p, "DUTY")) != nullptr) {
        rest = skip_space(rest);
        if (!*rest) return false;
        char *end;
        long v = strtol(rest, &end, 10);
        if (end == rest || !only_space(end)) return false;   // 숫자가 아니거나 뒤에 군더더기
        if (v < 0 || v > 255) return false;
        out->type = CMD_SET_DUTY;
        out->arg  = (int32_t)v;
        return true;
    }

    // "SELFTEST" — STATUS 보다 먼저 볼 필요는 없지만, 접두어가 겹치지 않는지 확인했다
    if ((rest = match_ci(p, "SELFTEST")) != nullptr && only_space(rest)) {
        out->type = CMD_SELFTEST;
        out->arg  = 0;
        return true;
    }

    // "STATUS"
    if ((rest = match_ci(p, "STATUS")) != nullptr && only_space(rest)) {
        out->type = CMD_STATUS;
        out->arg  = 0;
        return true;
    }

    return false;   // out->src 는 호출자가 채운다. 실패 시 *out 은 건드리지 않았다
}

const char *cmd_name(CmdType t) {
    switch (t) {
        case CMD_MOTOR_ON:  return "MOTOR ON";
        case CMD_MOTOR_OFF: return "MOTOR OFF";
        case CMD_SET_DUTY:  return "DUTY";
        case CMD_STATUS:    return "STATUS";
        case CMD_SELFTEST:  return "SELFTEST";
        default:            return "?";
    }
}

// ---------------------------------------------------------------------------
// 제출 — 여기서만 q_cmd 에 넣는다
// ---------------------------------------------------------------------------

bool cmd_submit(const Command *cmd) {
    if (!q_cmd) return false;
    // 대기 0. BLE 콜백 문맥에서 불릴 수 있으므로 절대 블로킹하면 안 된다.
    return xQueueSend(q_cmd, cmd, 0) == pdTRUE;
}

// ---------------------------------------------------------------------------
// 시리얼 입력 — BLE 없이 전체 명령 경로를 시험하는 통로
// ---------------------------------------------------------------------------

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
