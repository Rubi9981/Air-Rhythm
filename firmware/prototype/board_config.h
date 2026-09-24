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
const bool REPORT_SAMPLE = false; // 'raw<TAB>mv' 샘플 줄. 호스트 검증(run.sh) 시 true 필요.

static_assert(PERIOD_MS * (uint32_t)FS_HZ == 1000,
              "PERIOD_MS 와 breath_config.h 의 FS_HZ 가 어긋납니다");

// --- 타진 모터 (BL3640N BLDC + Nidec BLC-20 컨트롤러) ---
// BLC-20 은 정류(commutation)를 하지 않는다. 정류는 모터에 내장된 드라이버가 홀센서를
// 보고 알아서 하고, 여기서 내보내는 PWM 은 "목표 속도" 를 듀티로 인코딩한 지령이다.
// 그래서 이 핀의 20kHz 는 모터를 켰다 끄는 주파수가 아니다.
#define PIN_MOTOR_PWM    5       // BLC-20 PWM 입력
#define PIN_MOTOR_BRAKE  7       // BLC-20 BRAKE 입력
#define MOTOR_PWM_HZ     20000   // BLC-20 규격. 가청 대역 위라 조끼에서 소음이 나지 않는다
#define MOTOR_PWM_BITS   8       // duty 0~255

// DIR(방향) — GPIO 에 연결하지 않는다. 이 펌웨어는 방향을 바꾸지 않으므로
// 배선에서 레벨을 고정한다. 그래서 여기에 핀 번호가 없다.
//
//   정방향(현재 사용) : BLC-20 의 DIR 단자 → ESP32 의 VCC에 직결
//   역방향이 필요하면 : BLC-20 의 DIR 단자 → GND 에 직결
//
// 아두이노 예제가 정방향에 digitalWrite(dirPin, HIGH) 를 주는 것과 같은 상태다.
// BLC-20 에 자체 5V 출력 단자가 있으면 그쪽에 물려도 되지만, 그 5V 를 ESP32
// 쪽으로 끌어오지는 말 것 — ESP32 는 5V 톨러런트가 아니다.
//
// 3.3V 로 HIGH 가 인식되는 것은 실측으로 확인했다(BLC-20 입력 문턱이 3.2V 아래).
// 나중에 방향을 펌웨어에서 바꿔야 하면 이 단자를 빈 GPIO 로 옮기고
// act_motor.cpp 에 digitalWrite 를 추가하면 된다.

// 주의: BLDC 는 어느 듀티 아래로는 아예 돌지 않는다(약 15% = 38 부근으로 추정).
// 그 하한을 실측하는 것이 이 단계의 목적이므로 코드에서 클램프하지 않는다.

// ★ BLC-20 의 PWM 은 역논리다 — HIGH 가 정지, LOW 가 최대 속도.
// 아두이노 예제(Timer1, TOP=400)가 정지에 OCR1A=400(상시 HIGH)을 주는 것으로 확인했다.
// 그래서 LEDC 출력을 하드웨어 반전시킨다. 덕분에 motor_set() 쪽 의미는 그대로다
// (duty 0 = 정지, 255 = 최대). 반전은 act_motor.cpp 의 motor_init() 에서 건다.
#define MOTOR_PWM_INVERTED  true

// BRAKE 극성 — 실측 확인됨(아두이노 예제로 검증).
// 아두이노 코드가 brkPin 을 LOW 로 놓은 채 모터를 돌리므로 LOW 가 "해제" 다.
#define MOTOR_BRAKE_ON   HIGH
#define MOTOR_BRAKE_OFF  LOW

// 타진 강도 약/중/강 → PWM duty.
// ★ 임시값이다. 6단계에서 실측으로 정한다 — 단순히 33/66/100% 로 나누지 않고,
//   (1) 안정적으로 기동하는가 (2) 강도가 단계적으로 느는가 (3) 과열되지 않는가
//   (4) 브레이크로 충분히 빨리 서는가 를 보고 고른다. BLDC 는 약 38(15%) 아래에서
//   아예 돌지 않는 것으로 추정되므로 DUTY_LOW 는 그 위여야 한다.
#define DUTY_LOW    64
#define DUTY_MID    128
#define DUTY_HIGH   192

// 서비스 모드 — 시리얼 DUTY <n> 으로 화면 흐름을 거치지 않고 모터를 직접 돌린다.
// 위 duty 값을 실측할 때만 1 로 빌드한다. 평소 빌드에서는 반드시 0.
#define SERVICE_MODE 0

// 루프백 자가진단용 입력 핀. 계측기가 없을 때 PWM 이 실제로 핀에서 나가는지
// 확인하는 통로다. 점퍼선으로 PIN_MOTOR_PWM → 이 핀을 이어주고 SELFTEST 를 친다.
// 평소에는 아무것도 연결하지 않아도 되고, 연결해도 동작에 영향이 없다.
#define PIN_MOTOR_PWM_LOOPBACK    15
#define PIN_MOTOR_BRAKE_LOOPBACK  16
#define SELFTEST_WINDOW_MS        20   // 20kHz 기준 400주기. 이 동안 app_task 가 멈춘다

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
