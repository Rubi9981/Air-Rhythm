// 앱 태스크 — core 0. 센서가 보낸 스냅샷을 소비해 출력·판단·제어를 한다.
//
// 큐가 이 태스크의 페이싱을 대신한다(50Hz). 자체 delay 를 두지 않는 이유는,
// 큐 대기가 곧 "다음 샘플이 왔다"는 신호이기 때문이다.
//
// 하는 일: q_cmd 명령을 화면 상태 머신(app_logic)에 반영하고, 그 결과로 매 틱
// motor_set() 을 부르고, 화면·텔레메트리·시리얼 보고를 내보낸다.
// 판단은 app_logic.cpp 에 있고 이 태스크는 입출력만 잇는다.

#ifndef TASK_APP_H
#define TASK_APP_H

// 앱 태스크를 core 0 에 만든다. q_sense 가 만들어진 뒤에 부를 것.
void app_start();

#endif  // TASK_APP_H
