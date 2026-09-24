/**
 * ============================================================================
 * [link_cmd.cpp] 명령 파서 구현체 (BLE 바이너리 패킷 + 시리얼 텍스트)
 * ============================================================================
 *
 * 역할:
 *   - BLE로부터 들어온 8바이트 바이너리 패킷 파싱 (cmd_parse_packet)
 *   - 시리얼 모니터로부터 들어온 텍스트 명령 파싱 (cmd_parse)
 *   - 유효성이 검증된 Command 구조체를 FreeRTOS 큐(q_cmd)에 안전하게 삽입
 * ============================================================================
 */

#include "link_cmd.h"

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "link_msg.h"

// ============================================================================
// [1. BLE 바이너리 패킷 파서] - 8바이트 고정 패킷
// ============================================================================
/*
 * 패킷 구조 (8 Bytes):
 *   [0] 0xAA (Header 1)
 *   [1] 0x55 (Header 2)
 *   [2] Command ID (0x01: START, 0x02: STOP, 0x03: EMERGENCY, 0x04: CALIBRATE, 0x06: SET_DUTY)
 *   [3] Mode (0x01: Autonomous)
 *   [4] Duty (0~255, SET_DUTY 에서만 쓴다)
 *   [5] 0x00 (SET_DUTY 에서는 반드시 0)
 *   [6] Reserved (0x00)
 *   [7] Checksum (XOR of Bytes 2..6)
 *
 * 0x05(구 SET_PERIOD)는 폐기했다. 같은 번호를 SET_DUTY 로 재사용하지 않은 이유:
 * 옛 앱이 보내는 PERIOD 200~255 가 그대로 duty 로 읽혀 모터가 돌 수 있다.
 * 새 번호를 쓰면 옛 앱의 명령은 전부 거부되는 쪽으로 실패한다.
 */
bool cmd_parse_packet(const uint8_t *pkt, size_t len, Command *out) {
    if (pkt == nullptr || out == nullptr || len != 8) {
        return false;
    }

    // 1. 헤더 검증: 0xAA 0x55
    if (pkt[0] != 0xAA || pkt[1] != 0x55) {
        return false;
    }

    // 2. 체크섬 검증: Byte[2] ^ Byte[3] ^ Byte[4] ^ Byte[5] ^ Byte[6]
    uint8_t calculated_checksum = pkt[2] ^ pkt[3] ^ pkt[4] ^ pkt[5] ^ pkt[6];
    if (calculated_checksum != pkt[7]) {
        return false;
    }

    // 3. Command ID 매핑. 실패 경로에서는 *out 을 건드리지 않는다.
    switch (pkt[2]) {
        case 0x01:  // CMD_START (타격 허용)
            // [4-5] 는 무시한다. 옛 앱은 여기에 period(기본 500)를 담아 보낸다.
            out->type = CMD_START;
            out->arg  = 0;
            return true;

        case 0x02:  // CMD_STOP (정지)
        case 0x03:  // 하위 호환 정지 처리
            out->type = CMD_STOP;
            out->arg  = 0;
            return true;

        case 0x04:  // CMD_CALIBRATE (캘리브레이션 진입)
            out->type = CMD_CALIBRATE;
            out->arg  = 0;
            return true;

        case 0x06:  // CMD_SET_DUTY (0~255)
            // [5] 가 0 이 아니면 16비트 값을 보낸 것이다 — 255 를 넘는다는 뜻이므로 거부.
            if (pkt[5] != 0x00) return false;
            out->type = CMD_SET_DUTY;
            out->arg  = (int32_t)pkt[4];
            return true;

        default:    // 0x05(폐기) 포함
            return false;
    }
}

// ============================================================================
// [2. 시리얼 텍스트 명령 파서] - 디버깅 및 PC 제어용. 순수 함수.
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

bool cmd_parse(const char *line, Command *out) {
    if (!line || !out) return false;

    const char *p = skip_space(line);
    const char *rest;

    // "MOTOR ON" / "MOTOR OFF" — START / STOP 의 별칭
    if ((rest = match_ci(p, "MOTOR")) != nullptr) {
        rest = skip_space(rest);
        if (is_word(rest, "ON"))  return set_cmd(out, CMD_START, 0);
        if (is_word(rest, "OFF")) return set_cmd(out, CMD_STOP, 0);
        return false;
    }

    // "BREATH RESET" — 검출기 재정착(디버그용. 이후 호흡 모드 진입 흐름이 같은 명령을 쓴다)
    if ((rest = match_ci(p, "BREATH")) != nullptr) {
        rest = skip_space(rest);
        if (is_word(rest, "RESET")) return set_cmd(out, CMD_BREATH_RESET, 0);
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
        return set_cmd(out, CMD_SET_DUTY, (int32_t)v);
    }

    if (is_word(p, "START"))    return set_cmd(out, CMD_START, 0);
    if (is_word(p, "STOP"))     return set_cmd(out, CMD_STOP, 0);
    if (is_word(p, "ESTOP"))    return set_cmd(out, CMD_STOP, 0);
    if (is_word(p, "CAL"))      return set_cmd(out, CMD_CALIBRATE, 0);
    if (is_word(p, "STATUS"))   return set_cmd(out, CMD_STATUS, 0);
    if (is_word(p, "SELFTEST")) return set_cmd(out, CMD_SELFTEST, 0);

    return false;   // out->src 는 호출자가 채운다. 실패 시 *out 은 건드리지 않았다
}

// ============================================================================
// [3. 유틸리티 및 큐 삽입]
// ============================================================================

const char *cmd_name(CmdType t) {
    switch (t) {
        case CMD_START:             return "START";
        case CMD_STOP:              return "STOP";
        case CMD_SET_DUTY:          return "DUTY";
        case CMD_CALIBRATE:         return "CALIBRATE";
        case CMD_STATUS:            return "STATUS";
        case CMD_SELFTEST:          return "SELFTEST";
        case CMD_BREATH_RESET:      return "BREATH RESET";
        case CMD_BLE_CONNECTED:     return "BLE_CONNECTED";
        case CMD_BLE_DISCONNECTED:  return "BLE_DISCONNECTED";
        default:                    return "?";
    }
}

bool cmd_submit(const Command *cmd) {
    if (!q_cmd || cmd == nullptr) return false;

    // 대기 0. BLE 콜백 문맥에서 불릴 수 있으므로 절대 블로킹하면 안 된다.
    return xQueueSend(q_cmd, cmd, 0) == pdTRUE;
}

// 시리얼 입력 — BLE 없이 전체 명령 경로를 시험하는 통로
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
