// 보드 설정 — 핀 배치, 표본화, 타진 모터, 태스크 배치.
//
// 검출 파라미터는 breath_config.h 에 있다. 여기 있는 것은 "이 기판이 어떻게 배선되어 있고
// 어떻게 표본화하는가" 이며, 두 파일의 FS_HZ / PERIOD_MS 는 아래 static_assert 로 묶여 있다.

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdint.h>

#include "breath_config.h"

// ============================================================================
// 핀 배치 — ESP32-S3 에 연결된 GPIO 는 모두 여기에만 적는다.
// 실제 배선(납땜)과 1:1 이다. 배선을 바꾸면 이 표와 아래 #define 만 고친다.
//
//  GPIO | 이름                      | 연결 대상                        | 비고
//  -----+---------------------------+----------------------------------+------------------------------
//    4  | PIN_BREATH_ADC            | 전도성 고무 호흡 센서(RS1)       | ADC1. WiFi 를 쓰려면 ADC1(1~10) 이어야 한다
//    5  | PIN_MOTOR_PWM             | BLC-20 PWM                       | 역논리 — HIGH 가 정지
//    7  | PIN_MOTOR_BRAKE           | BLC-20 BRAKE                     | 극성 미확정 — 아래 MOTOR_BRAKE_ON 참조
//    8  | PIN_LCD_SDA               | 레벨 컨버터 LV1 → HV1 → LCD SDA  | I2C (5단계)
//    9  | PIN_LCD_SCL               | 레벨 컨버터 LV2 → HV2 → LCD SCL  | I2C (5단계)
//   10  | PIN_KEY_RIGHT             | 5방향 스위치 오른쪽              | 누르면 LOW — COM 이 GND, 내부 풀업 (4단계)
//   11  | PIN_KEY_OK                | 5방향 스위치 가운데              |   〃
//   12  | PIN_KEY_DOWN              | 5방향 스위치 아래                |   〃
//   13  | PIN_KEY_UP                | 5방향 스위치 위                  |   〃
//   14  | PIN_KEY_LEFT              | 5방향 스위치 왼쪽                |   〃
//   15  | PIN_MOTOR_PWM_LOOPBACK    | SELFTEST 점퍼 (GPIO5 → 15)       | 평소에는 비워 둔다
//   16  | PIN_MOTOR_BRAKE_LOOPBACK  | SELFTEST 점퍼 (GPIO7 → 16)       | 평소에는 비워 둔다
//
// GPIO 가 아닌 배선:
//   BLC-20 DIR       → 3V3 직결(정방향). 방향을 바꾸지 않으므로 핀을 쓰지 않는다 — 아래 모터 절 참조.
//   레벨 컨버터 LV   → 3V3,  레벨 컨버터 HV · LCD VCC → 5V,  스위치 COM · 모든 모듈 GND → GND
//
// ★ 모터 두 핀(5, 7)은 전원 투입부터 motor_init() 까지 떠 있다. 그 사이 BLC-20 이 "정지 + 제동" 으로
//   읽도록 외부 저항(10kΩ)을 달아야 한다 — PWM 은 3V3 으로 풀업, BRAKE 는 제동 레벨 쪽으로.
//   BRAKE 극성을 24V 로 확인한 뒤 풀업/풀다운을 정할 것.
//
// 피한 핀: GPIO0·3·45·46 (부팅 모드 결정), GPIO19·20 (USB).
// ============================================================================
#define PIN_BREATH_ADC            4
#define PIN_MOTOR_PWM             5
#define PIN_MOTOR_BRAKE           7
#define PIN_LCD_SDA               8
#define PIN_LCD_SCL               9
#define PIN_KEY_RIGHT             10
#define PIN_KEY_OK                11
#define PIN_KEY_DOWN              12
#define PIN_KEY_UP                13
#define PIN_KEY_LEFT              14
#define PIN_MOTOR_PWM_LOOPBACK    15
#define PIN_MOTOR_BRAKE_LOOPBACK  16

// 같은 GPIO 를 두 곳에 적었으면 컴파일이 멈춘다. 새 핀을 추가하면 이 목록에도 넣을 것.
constexpr int ALL_PINS[] = {
    PIN_BREATH_ADC, PIN_MOTOR_PWM, PIN_MOTOR_BRAKE, PIN_LCD_SDA, PIN_LCD_SCL,
    PIN_KEY_RIGHT, PIN_KEY_OK, PIN_KEY_DOWN, PIN_KEY_UP, PIN_KEY_LEFT,
    PIN_MOTOR_PWM_LOOPBACK, PIN_MOTOR_BRAKE_LOOPBACK,
};
constexpr bool pins_are_unique() {
    for (unsigned i = 0; i < sizeof ALL_PINS / sizeof ALL_PINS[0]; i++)
        for (unsigned j = i + 1; j < sizeof ALL_PINS / sizeof ALL_PINS[0]; j++)
            if (ALL_PINS[i] == ALL_PINS[j]) return false;
    return true;
}
static_assert(pins_are_unique(), "board_config.h 핀 배치에 같은 GPIO 가 두 번 들어 있습니다");

// ============================================================================
// 표본화
// ============================================================================
const uint32_t PERIOD_MS = 20;   // 표본화 주기 → 50Hz. breath_config.h 의 FS_HZ 와 짝
const int ADC_AVG_COUNT = 16;            // mV 평균 횟수(약 1.9ms). 줄이면 기울기 검출의 노이즈 여유가 준다
const bool REPORT_RATE = true;   // 1초마다 '# fs=...' 진단 줄. 측정이 끝나면 false 로
const bool REPORT_SAMPLE = false; // 'raw<TAB>mv' 샘플 줄. 호스트 검증(run.sh) 시 true 필요.

static_assert(PERIOD_MS * (uint32_t)FS_HZ == 1000,
              "PERIOD_MS 와 breath_config.h 의 FS_HZ 가 어긋납니다");

// ============================================================================
// 타진 모터 (BL3640N BLDC + Nidec BLC-20 컨트롤러)
// ============================================================================
// BLC-20 은 정류(commutation)를 하지 않는다. 정류는 모터에 내장된 드라이버가 홀센서를
// 보고 알아서 하고, 여기서 내보내는 PWM 은 "목표 속도" 를 듀티로 인코딩한 지령이다.
// 그래서 이 핀의 20kHz 는 모터를 켰다 끄는 주파수가 아니다.
// 핀 번호는 맨 위 핀 배치(PIN_MOTOR_PWM / PIN_MOTOR_BRAKE)에 있다.
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

// BRAKE 극성 — 아두이노 예제로 추정한 값이다(예제가 brkPin 을 LOW 로 놓은 채 모터를 돌렸다).
// ★ 판매처 최신 예제(V3.2)는 반대로 LOW 를 "브레이크 ON" 으로 쓴다. 24V 로 모터를 돌리면서
//   BRAKE 레벨을 바꿔 어느 쪽에서 급정지하는지 직접 확인하고 아래 두 값을 확정할 것.
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

// 루프백 자가진단 — 계측기가 없을 때 PWM 이 실제로 핀에서 나가는지 확인하는 통로다.
// 점퍼선으로 PIN_MOTOR_PWM → PIN_MOTOR_PWM_LOOPBACK 을 잇고 SELFTEST 를 친다(핀은 맨 위 핀 배치).
// 평소에는 아무것도 연결하지 않아도 되고, 연결해도 동작에 영향이 없다.
#define SELFTEST_WINDOW_MS        20   // 20kHz 기준 400주기. 이 동안 app_task 가 멈춘다

// ============================================================================
// 태스크 배치
// ============================================================================
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
