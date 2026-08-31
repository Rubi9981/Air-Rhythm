// 앱 태스크 — core 0. 센서가 보낸 스냅샷을 소비해 출력·판단·제어를 한다.
//
// 큐가 이 태스크의 페이싱을 대신한다(50Hz). 자체 delay 를 두지 않는 이유는,
// 큐 대기가 곧 "다음 샘플이 왔다"는 신호이기 때문이다.
//
// 0단계에서는 출력만 한다. 앞으로 여기에 붙을 것:
//   - 앱 명령 반영 (q_cmd)
//   - 기능 판단 → Intent
//   - 중재·안전 검사 → 모터 출력
//   - BLE notify

#ifndef TASK_APP_H
#define TASK_APP_H

// 앱 태스크를 core 0 에 만든다. q_sense 가 만들어진 뒤에 부를 것.
void app_start();

#endif  // TASK_APP_H
