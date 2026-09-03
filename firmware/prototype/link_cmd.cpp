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
#include <string.h>
#include <ctype.h>

#include "link_msg.h"

// ============================================================================
// [1. BLE 바이너리 패킷 파서] - 8바이트 고정 패킷
// ============================================================================
/*
 * 패킷 구조 (8 Bytes):
 *   [0] 0xAA (Header 1)
 *   [1] 0x55 (Header 2)
 *   [2] Command ID (0x01: START, 0x02: STOP, 0x03: EMERGENCY, 0x04: CALIBRATE, 0x05: SET_PERIOD)
 *   [3] Mode (0x01: Autonomous)
 *   [4] Period Low Byte (Little Endian, ms)
 *   [5] Period High Byte (Little Endian, ms)
 *   [6] Reserved (0x00)
 *   [7] Checksum (XOR of Bytes 2..6)
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

    // 3. 파라미터 추출 (Little-Endian 타격 주기)
    uint16_t periodMs = (uint16_t)pkt[4] | ((uint16_t)pkt[5] << 8);

    out->arg = 0;

    // 4. Command ID 매핑
    switch (pkt[2]) {
        case 0x01:  // CMD_START (타격 시작)
            out->type = CMD_START;
            out->arg = (periodMs >= 200 && periodMs <= 2000) ? (int32_t)periodMs : 500;
            break;

        case 0x02:  // CMD_STOP (정상 정지)
            out->type = CMD_STOP;
            break;

        case 0x03:  // CMD_EMERGENCY_STOP (긴급 정지)
            out->type = CMD_EMERGENCY_STOP;
            break;

        case 0x04:  // CMD_CALIBRATE (캘리브레이션 진입)
            out->type = CMD_CALIBRATE;
            break;

        case 0x05:  // CMD_SET_PERIOD (동작 중 주기 실시간 변경)
            if (periodMs < 200 || periodMs > 2000) {
                return false;
            }
            out->type = CMD_SET_PERIOD;
            out->arg = (int32_t)periodMs;
            break;

        default:
            return false;
    }

    return true;
}

// ============================================================================
// [2. 시리얼 텍스트 명령 파서] - 디버깅 및 PC 제어용
// ============================================================================

// 공백 문자 건너뛰기 헬퍼
static const char* skip_whitespace(const char *str) {
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

// 대소문자 무시 접두어 비교 헬퍼
static const char* match_prefix_ci(const char *str, const char *prefix) {
    while (*prefix) {
        if (toupper((unsigned char)*str) != toupper((unsigned char)*prefix)) {
            return nullptr;
        }
        str++;
        prefix++;
    }
    return str;
}

bool cmd_parse(const char *line, Command *out) {
    if (line == nullptr || out == nullptr) {
        return false;
    }

    const char *p = skip_whitespace(line);
    const char *rest = nullptr;

    // "START <period_ms>" -> CMD_START
    if ((rest = match_prefix_ci(p, "START")) != nullptr) {
        rest = skip_whitespace(rest);
        int32_t period = 500;
        if (*rest) {
            period = (int32_t)atoi(rest);
            if (period < 200 || period > 2000) return false;
        }
        out->type = CMD_START;
        out->arg = period;
        return true;
    }

    // "ESTOP" -> CMD_EMERGENCY_STOP (STOP보다 먼저 확인)
    if (match_prefix_ci(p, "ESTOP") != nullptr) {
        out->type = CMD_EMERGENCY_STOP;
        out->arg = 0;
        return true;
    }

    // "STOP" -> CMD_STOP
    if (match_prefix_ci(p, "STOP") != nullptr) {
        out->type = CMD_STOP;
        out->arg = 0;
        return true;
    }

    // "PERIOD <period_ms>" -> CMD_SET_PERIOD
    if ((rest = match_prefix_ci(p, "PERIOD")) != nullptr) {
        rest = skip_whitespace(rest);
        if (!*rest) return false;
        int32_t period = (int32_t)atoi(rest);
        if (period < 200 || period > 2000) return false;
        out->type = CMD_SET_PERIOD;
        out->arg = period;
        return true;
    }

    // "CAL" -> CMD_CALIBRATE
    if (match_prefix_ci(p, "CAL") != nullptr) {
        out->type = CMD_CALIBRATE;
        out->arg = 0;
        return true;
    }

    // "STATUS" -> CMD_STATUS
    if (match_prefix_ci(p, "STATUS") != nullptr) {
        out->type = CMD_STATUS;
        out->arg = 0;
        return true;
    }

    return false;
}

// ============================================================================
// [3. 유틸리티 및 큐 삽입]
// ============================================================================

const char* cmd_name(CmdType t) {
    switch (t) {
        case CMD_START:             return "START";
        case CMD_STOP:              return "STOP";
        case CMD_EMERGENCY_STOP:    return "EMERGENCY_STOP";
        case CMD_SET_PERIOD:        return "SET_PERIOD";
        case CMD_CALIBRATE:         return "CALIBRATE";
        case CMD_STATUS:            return "STATUS";
        case CMD_BLE_CONNECTED:     return "BLE_CONNECTED";
        case CMD_BLE_DISCONNECTED:  return "BLE_DISCONNECTED";
        default:                    return "UNKNOWN";
    }
}

bool cmd_submit(const Command *cmd) {
    if (!q_cmd || cmd == nullptr) return false;

    // 대기 시간 0: 콜백 컨텍스트에서도 블로킹 없이 안전하게 삽입
    return xQueueSend(q_cmd, cmd, 0) == pdTRUE;
}

void cmd_poll_serial() {
    static char   buf[64];
    static size_t len = 0;

    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == '\n' || c == '\r') {
            if (len > 0) {
                buf[len] = '\0';
                Command cmd = {};
                if (cmd_parse(buf, &cmd)) {
                    cmd.src = SRC_SERIAL;
                    cmd_submit(&cmd);
                } else {
                    msg_ack_err(SRC_SERIAL, "unknown_command");
                }
                len = 0;
            }
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = c;
        } else {
            // 버퍼 오버플로우 방지: 초과 라인은 리셋하여 삭제
            len = 0;
        }
    }
}
