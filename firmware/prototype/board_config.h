// 보드·표본화 상수 — breath_monitor.ino 상단 블록이 그대로 옮겨왔다.
//
// 검출 파라미터는 breath_config.h 에 있다. 여기 있는 것은 "이 기판에서 어떻게
// 표본화하는가" 뿐이며, 두 파일의 FS_HZ / PERIOD_MS 는 아래 static_assert 로 묶여 있다.

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdint.h>

#include "breath_config.h"

const int SENSOR_PIN = 4;        // ADC1 채널 GPIO. WiFi 쓸 거면 반드시 ADC1(GPIO 1~10)
const uint32_t PERIOD_MS = 20;   // 표본화 주기 → 50Hz. breath_config.h 의 FS_HZ 와 짝
const int ADC_AVG_COUNT = 16;            // mV 평균 횟수(약 1.9ms). 줄이면 기울기 검출의 노이즈 여유가 준다
const bool REPORT_RATE = true;   // 1초마다 '# fs=...' 진단 줄. 측정이 끝나면 false 로
const bool REPORT_SAMPLE = false; // 'raw<TAB>mv' 샘플 줄. raw 값과 mv 값을 확인할 때 true. 일반적으로 false.

static_assert(PERIOD_MS * (uint32_t)FS_HZ == 1000,
              "PERIOD_MS 와 breath_config.h 의 FS_HZ 가 어긋납니다");

// --- 온보드 RGB LED (진단 표시) ---
// ESP32-S3 개발보드의 WS2812 한 개. 모터 하드웨어가 오기 전까지 "BLE 명령이
// 도착했는가" 를 눈으로 확인하는 창이고, 이후에도 진단 표시로 계속 쓴다.
#define RGB_PIN        48
#define RGB_MAX_LEVEL  40    // 0~255 중 상한. 온보드 LED 는 매우 밝고 전류도 먹는다
#define FLASH_TICKS    5     // 명령 도착 플래시 지속 틱 (20ms × 5 = 100ms)

// --- 태스크 배치 ---
// 센서는 core 1 독점. BLE 스택은 기본 설정상 core 0 에 붙으므로 물리적으로 격리된다.
#define SENSE_CORE      1
#define SENSE_PRIORITY  5
#define SENSE_STACK     4096

#define APP_CORE        0
#define APP_PRIORITY    2
#define APP_STACK       8192   // Serial.printf("%f") 의 부동소수 포맷팅 + 이후 BLE 대비

// 50Hz × 32 = 640ms 버퍼. core 0 이 순간 바빠도(BLE 연결 수립 등) 샘플을 잃지 않는다.
#define Q_SENSE_DEPTH 32

// 명령은 사람이 앱에서 누르는 속도로만 온다. 8이면 충분하다.
#define Q_CMD_DEPTH   8

// 이 시간 동안 샘플이 한 개도 안 오면 센서 태스크가 멈춘 것으로 본다(데드맨).
// 정상이면 20ms 마다 오므로 여유가 크다.
#define SENSE_STALL_MS 200

#endif  // BOARD_CONFIG_H
