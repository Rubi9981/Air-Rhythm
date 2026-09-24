// 명령 파서 — 시리얼(텍스트)과 BLE(바이너리)가 공유한다.
//
// 이 계층은 "무엇을 하라는 말인지"만 해석한다. 상태를 바꾸거나 하드웨어를
// 건드리지 않는다. 그 일은 app_task 가 q_cmd 에서 꺼내 처리한다.
//
// 공유하는 이유: BLE 를 붙이기 전에 시리얼로 모든 명령을 테스트할 수 있다.
// BLE 디버깅과 로직 디버깅이 섞이지 않는 것이 개발 속도를 가장 크게 좌우한다.
//
// [바이너리] 앱 → ESP32 8바이트 패킷:
//   [0] 0xAA [1] 0x55 [2] cmdId [3] mode [4] duty [5] 0x00 [6] rsvd [7] checksum
//   cmdId: 0x01 START, 0x02/0x03 STOP, 0x04 CALIBRATE, 0x06 SET_DUTY(0~255)
//   0x05(구 SET_PERIOD)는 폐기 — 받으면 거부한다.
//
// [텍스트] 시리얼 명령 (한 줄 = 한 명령, 대소문자 무시):
//   START | MOTOR ON | STOP | ESTOP | MOTOR OFF | DUTY <0-255> | CAL | STATUS | SELFTEST
//   BREATH RESET (호흡 검출기 재정착)
//   SELFTEST 는 시리얼 전용이다(실행 중 app_task 가 멈추므로 앱에서 부를 수 없게 한다).

#ifndef LINK_CMD_H
#define LINK_CMD_H

#include <stdint.h>
#include <stddef.h>

#include "app_types.h"

// 8바이트 바이너리 패킷을 Command 로 해석한다 (BLE Write 콜백용).
// 헤더(0xAA,0x55), 체크섬 검증 포함. 실패 시 false.
// out->src 는 호출자가 채운다.
bool cmd_parse_packet(const uint8_t *pkt, size_t len, Command *out);

// 한 줄 텍스트를 Command 로 해석한다 (시리얼 디버그용). 부수효과 없는 순수 함수 —
// 호스트에서 단위 테스트할 수 있도록 이 성질을 유지할 것.
// 값 검증까지 여기서 끝낸다(예: DUTY 는 0~255). 경계에서 거르므로 app_task 는
// "이미 유효한 명령"만 다루면 된다. 실패 시 false 이고 *out 은 건드리지 않는다.
// out->src 는 호출자가 채운다.
bool cmd_parse(const char *line, Command *out);

// Command 를 q_cmd 에 넣는다. 대기 0 이므로 BLE 콜백 문맥에서 불러도 안전하다.
bool cmd_submit(const Command *cmd);

// 시리얼 입력을 한 줄씩 모아 파싱해 q_cmd 로 넘긴다. app_task 가 매 틱 부른다.
void cmd_poll_serial();

// ACK 문자열용. 사람이 읽는 이름.
const char *cmd_name(CmdType t);

#endif  // LINK_CMD_H
