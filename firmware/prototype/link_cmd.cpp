#include "link_cmd.h"

#include <Arduino.h>
#include <string.h>

#include "link_msg.h"

// ---------------------------------------------------------------------------
// 파싱 — 순수 함수. Serial 도 BLE 도 여기로 들어온다.
// ---------------------------------------------------------------------------

bool cmd_parse(const char *line, Command *out) {
    // TODO(구현): 앞뒤 공백 제거, 대소문자 무시 비교, 다음 문법을 인식할 것
    //   "MOTOR ON"   → CMD_MOTOR_ON
    //   "MOTOR OFF"  → CMD_MOTOR_OFF
    //   "DUTY 200"   → CMD_SET_DUTY,  arg = 200
    //   "STATUS"     → CMD_STATUS
    // 인식 못 하면 false. out->src 는 호출자가 채운다.
    //
    // 값 검증도 여기서 끝낸다 — DUTY 는 0~255 를 벗어나면 false 를 돌려준다.
    // 경계에서 거르면 app_task 는 "이미 유효한 명령"만 다루면 되고, 잘못된 값이
    // 시스템 안으로 들어오는 경로가 하나도 없다.
    (void)line; (void)out;
    return false;
}

const char *cmd_name(CmdType t) {
    switch (t) {
        case CMD_MOTOR_ON:  return "MOTOR ON";
        case CMD_MOTOR_OFF: return "MOTOR OFF";
        case CMD_SET_DUTY:  return "DUTY";
        case CMD_STATUS:    return "STATUS";
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
    static char  buf[64];
    static size_t len = 0;

    // TODO(구현): Serial.available() 만큼만 읽어 '\n' 까지 buf 에 모은다.
    //   - 줄이 완성되면 cmd_parse() → src = SRC_SERIAL → cmd_submit()
    //   - 파싱 실패면 msg_ack_err(SRC_SERIAL, "unknown")
    //   - buf 가 넘치면 그 줄은 버리고 len = 0 (여기서 블로킹하지 말 것)
    (void)buf; (void)len;
}
