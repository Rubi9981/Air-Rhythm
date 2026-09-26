// 센서 태스크 — core 1 전용. 정확히 20ms 주기로 ADC → 대역통과 → 검출.
//
// 이 태스크 안에서는 Serial / BLE / 모터 / 블로킹 호출을 하지 않는다. 그것이
// 20ms 주기를 지키는 유일한 조건이고, 검출 정확도가 여기에 걸려 있다
// (필터·EMA 계수가 전부 dt=20ms 고정을 전제로 설계됨).
//
// 필터와 검출기 상태(BandpassButter2 / SlopeDetector)는 task_sense.cpp 의 static
// 이며 밖으로 노출하지 않는다. 앱 태스크는 SenseUpdate 사본만 본다.

#ifndef TASK_SENSE_H
#define TASK_SENSE_H

#include <stdint.h>

#include "app_types.h"

// 필터·검출기를 초기화하고 센서 태스크를 core 1 에 만든다.
// q_sense 가 만들어진 뒤에 부를 것.
void sense_start();

// 검출기 초기화를 "요청"한다. 실제 초기화는 센서 태스크가 다음 틱 시작에 자기 코어에서 한다 —
// 필터·검출기 상태는 센서 태스크 소유라, 다른 태스크가 sense_reset() 을 직접 부르면
// 진행 중인 sense_step() 과 경쟁한다.
//
// 반환값은 새 회차 번호다. 이후 SenseUpdate.epoch 가 이 값과 같아진 샘플부터가
// 초기화 뒤의 샘플이다(= "초기화가 시작된 것을 확인"). 그 전 샘플은 버려야 한다.
//
// 쓰는 쪽은 app_task 하나여야 한다(잠금 없이 단일 작성자를 전제로 한다).
uint16_t sense_request_reset();

// --- 획득과 처리의 경계 ---
// 아래 둘은 태스크 루프 없이도 부를 수 있다. 덕분에 (1) 호스트에서 녹음 CSV 로
// 출력을 대조 검증할 수 있고, (2) 나중에 획득을 타이머 ISR + 링버퍼로 옮길 때
// 처리 쪽을 그대로 재사용할 수 있다.
void sense_reset();                                        // 필터·검출기 초기화 (태스크 시작 전 / 센서 태스크 안에서만)
void sense_step(int16_t raw, int16_t mv, SenseUpdate *pending);   // 한 샘플 처리

#endif  // TASK_SENSE_H
