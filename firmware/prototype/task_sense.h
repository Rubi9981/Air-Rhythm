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

// --- 획득과 처리의 경계 ---
// 아래 둘은 태스크 루프 없이도 부를 수 있다. 덕분에 (1) 호스트에서 녹음 CSV 로
// 출력을 대조 검증할 수 있고, (2) 나중에 획득을 타이머 ISR + 링버퍼로 옮길 때
// 처리 쪽을 그대로 재사용할 수 있다.
void sense_reset();                                        // 필터·검출기 초기화
void sense_step(int16_t raw, int16_t mv, SenseUpdate *pending);   // 한 샘플 처리

#endif  // TASK_SENSE_H
