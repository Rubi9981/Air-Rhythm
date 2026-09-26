// 명령 파서 — 시리얼 텍스트.
//
// 이 계층은 "무엇을 하라는 말인지"만 해석한다. 상태를 바꾸거나 하드웨어를
// 건드리지 않는다. 그 일은 app_task 가 q_cmd 에서 꺼내 처리한다.
//
// 모터를 켜는 경로는 기기 버튼(화면 흐름) 하나뿐이다. 시리얼의 BTN 은 물리 버튼과
// 같은 CMD_BUTTON 으로 들어가므로 흐름을 건너뛰지 않는다. BLE 는 모니터링 전용이라
// 앱에서 오는 명령은 받지 않는다.
//
// [텍스트] 시리얼 명령 (한 줄 = 한 명령, 대소문자 무시):
//   BTN U|D|L|R|OK (UP/DOWN/LEFT/RIGHT 도 가능)  — 버튼 흉내
//   STOP | ESTOP                                   — 어느 화면에서든 정지
//   STATUS                                         — 상태 한 줄
//   SELFTEST                                       — 배선 검증. 정지 중에만
//   BREATH RESET                                   — 호흡 검출기 재정착
//   DUTY <0-255>                                   — 서비스 모드(SERVICE_MODE=1) 빌드 전용 직접 구동

#ifndef LINK_CMD_H
#define LINK_CMD_H

#include <stdint.h>
#include <stddef.h>

#include "app_types.h"

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
const char *button_name(Button b);

#endif  // LINK_CMD_H
