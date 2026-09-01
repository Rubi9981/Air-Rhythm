// 명령 파서 — 시리얼과 BLE 가 공유한다.
//
// 이 계층은 "무엇을 하라는 말인지"만 해석한다. 상태를 바꾸거나 하드웨어를
// 건드리지 않는다. 그 일은 app_task 가 q_cmd 에서 꺼내 처리한다.
//
// 공유하는 이유: BLE 를 붙이기 전에 시리얼로 모든 명령을 테스트할 수 있다.
// BLE 디버깅과 로직 디버깅이 섞이지 않는 것이 개발 속도를 가장 크게 좌우한다.
//
// 명령 문법 (한 줄 = 한 명령, 대소문자 무시):
//   MOTOR ON | MOTOR OFF | DUTY <0-255> | STATUS

#ifndef LINK_CMD_H
#define LINK_CMD_H

#include "app_types.h"

// 한 줄을 Command 로 해석한다. 부수효과 없는 순수 함수 —
// 호스트에서 단위 테스트할 수 있도록 이 성질을 유지할 것.
//
// 값 검증까지 여기서 끝낸다(예: DUTY 는 0~255). 경계에서 거르므로 app_task 는
// "이미 유효한 명령"만 다루면 된다.
// 반환: 해석 성공 여부. 실패 시 *out 은 건드리지 않는다.
bool cmd_parse(const char *line, Command *out);

// Command 를 q_cmd 에 넣는다. 대기 0 이므로 BLE 콜백 문맥에서 불러도 안전하다.
// 반환: 큐에 들어갔는지 여부(가득 차면 false).
bool cmd_submit(const Command *cmd);

// 시리얼 입력을 한 줄씩 모아 파싱해 q_cmd 로 넘긴다. app_task 가 매 틱 부른다.
// 블로킹하지 않는다 — 도착한 만큼만 읽는다.
void cmd_poll_serial();

// ACK 문자열용. "MOTOR ON" 같은 사람이 읽는 이름.
const char *cmd_name(CmdType t);

#endif  // LINK_CMD_H
