// 전도성 고무 호흡 센서 — 프로토타입 펌웨어
//
// breath_monitor 의 단일 loop() 구조를 두 태스크로 나눈 것이다. 검출 로직
// (breath_config.h / breath_filter.* / breath_slope.*)은 복사본이며 수정하지 않았다.
//
//   core 1  sense_task (prio 5)  ADC → 대역통과 → 검출 → q_sense        정확히 20ms
//   core 0  app_task   (prio 2)  q_sense → 출력 (이후 모터·BLE)         큐가 페이싱
//   core 1  loopTask   (prio 1)  재워둔다
//
// 왜 나눴는가: Serial.printf 는 115200 baud 에서 40자 한 줄이 약 3.5ms 이고 TX 버퍼가
// 차면 블로킹한다. BLE 는 연결 이벤트 간격만큼 더 튄다. 이것들이 표본화 루프 안에
// 있으면 dt 가 흔들리는데, 필터·EMA 계수가 전부 dt=20ms 고정을 전제로 설계돼 있어
// (breath_config.h) 차단주파수가 밀리고 검출이 조용히 나빠진다. 태스크를 나누고
// 코어를 갈라두면 core 0 에서 무슨 일이 나든 표본화는 영향을 받지 않는다.
//
// 시리얼 규약과 출력 형식은 link_msg.h 참조. 기존 호스트 도구가 그대로 동작한다.

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "app_types.h"
#include "board_config.h"
#include "link_ble.h"
#include "task_app.h"
#include "task_sense.h"

QueueHandle_t q_sense = nullptr;
QueueHandle_t q_cmd   = nullptr;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);                       // 기본 12비트(0~4095)
  analogSetPinAttenuation(SENSOR_PIN, ADC_6db);   // 0~1750mV (ESP32-S3)

  // ADC 설정과 실제 읽기가 같은 코어(core 1)에서 일어난다 — setup() 은 loopTask 에서
  // 돌고 loopTask 는 core 1 이며, sense_task 도 core 1 이다.

  q_sense = xQueueCreate(Q_SENSE_DEPTH, sizeof(SenseUpdate));
  q_cmd   = xQueueCreate(Q_CMD_DEPTH,   sizeof(Command));
  if (!q_sense || !q_cmd) {
    Serial.println("# FATAL queue");
    while (true) delay(1000);
  }

  // 순서: 큐 → BLE → 태스크.
  // BLE 콜백이 q_cmd 에 넣으므로 큐가 먼저 있어야 하고, app_task 는 BLE 가 준비된
  // 뒤에 도는 편이 안전하다. ble_init() 이 길어져도 표본화 주기에는 영향이 없다 —
  // sense_task 의 기준 시각은 태스크 진입부에서 잡는다.
  ble_init();

  sense_start();
  app_start();
}

void loop() {
  // 두 태스크가 알아서 돈다. loopTask 는 core 1 에 있으므로 재워서 센서 전용으로 둔다.
  vTaskDelay(portMAX_DELAY);
}
