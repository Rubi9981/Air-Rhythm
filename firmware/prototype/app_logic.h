// 화면·모드 상태 머신과 모터 출력 판단 — 순수 로직.
//
// Arduino / FreeRTOS / Serial 을 부르지 않는다. 시간은 인자(now_ms)로 받는다.
// 그래서 호스트에서 버튼 시나리오를 그대로 재생해 검증할 수 있다(host_test/logic.sh).
// 입출력(큐, motor_set, 메시지)은 task_app.cpp 가 맡는다.
//
// --- 지켜야 할 것 ---
//  1. 모터 출력은 logic_output() 한 곳에서만 정한다. 화면이 RUN 이 아니면 0 이다.
//  2. FAULT 는 한 번 들어가면 나오는 길이 없다. 전원을 다시 켜야 풀린다.
//  3. 부팅 직후는 항상 모드 선택 화면 + 강도 LOW + 출력 0 이다. 이전 동작을 이어가지 않는다.
//
// --- 화면 흐름 (2단계 범위) ---
//   MODE_SELECT ─OK→ NORMAL_SETUP ─START→ NORMAL_RUN ─OK(STOP)→ MODE_SELECT
//               ─OK→ BREATH_SETUP                  (호흡 모드 실행은 3단계)
//   어디서든 ─고장→ FAULT (끝)
//   서비스 빌드에서만: MODE_SELECT ─시리얼 DUTY n→ SERVICE ─STOP/OK/DUTY 0→ MODE_SELECT

#ifndef APP_LOGIC_H
#define APP_LOGIC_H

#include <stdint.h>

#include "app_types.h"

// 화면이 바뀐 직후 이 시간 동안은 버튼을 무시한다 — OK 를 빠르게 두 번 누른 것이
// 다음 화면으로 넘어가 곧바로 타진이 시작되는 일을 막는다. 단, 실행 화면의 STOP 은 예외다.
#define INPUT_LOCKOUT_MS     200

// 큐 backlog 가 이 틱 수만큼 이어지면 앱 태스크 과부하로 보고 FAULT (50틱 = 1초).
#define BACKLOG_FAULT_TICKS  50

typedef enum {
    SCR_MODE_SELECT = 0,
    SCR_NORMAL_SETUP,
    SCR_NORMAL_RUN,
    SCR_BREATH_SETUP,
    SCR_SERVICE,
    SCR_FAULT,
    SCR_COUNT,
} Screen;

typedef enum {
    LEVEL_LOW = 0,
    LEVEL_MID,
    LEVEL_HIGH,
    LEVEL_COUNT,
} Level;

typedef enum {
    FAULT_NONE = 0,
    FAULT_SENSE_STALL,      // SENSE_STALL_MS 동안 샘플 0개 — 센서 태스크 멈춤
    FAULT_SENSE_OVERLOAD,   // 샘플 유실(QDROP) 또는 backlog 1초 지속 — 앱 태스크 과부하
    FAULT_BAD_STATE,        // 화면·강도 값이 범위 밖 — 메모리 오염이나 코드 버그
} Fault;

// 설정 화면의 커서 위치
enum { SETUP_POWER = 0, SETUP_START, SETUP_BACK, SETUP_ITEMS };

typedef struct {
    Screen   screen;
    uint8_t  cursor;           // 화면 안의 선택 위치
    Level    level;            // 타진 강도
    Fault    fault;            // FAULT_NONE 이 아니면 screen == SCR_FAULT
    uint32_t screen_since_ms;  // 이 화면에 들어온 시각. 입력 잠금 기준
    uint16_t backlog_ticks;    // backlog 가 연속된 틱 수
    uint8_t  service_duty;     // SCR_SERVICE 에서만 의미가 있다
} AppModel;

#define LCD_COLS 16
typedef char ScreenLines[2][LCD_COLS + 1];   // LCD 두 줄. 각 줄은 정확히 16자 + '\0'

void logic_init(AppModel *m, uint32_t now_ms);

// 버튼 하나를 반영한다. 잠금·FAULT 로 무시했으면 false.
bool logic_button(AppModel *m, Button b, uint32_t now_ms);

// 원격 정지(시리얼 STOP). 실행 중이면 멈추고 모드 선택으로 간다. FAULT 는 그대로 둔다.
void logic_stop(AppModel *m, uint32_t now_ms);

// 서비스 모드 직접 구동. duty 0 은 정지. 모드 선택·서비스 화면에서만 받는다(아니면 false).
// SERVICE_MODE 빌드인지는 호출자가 확인한다.
bool logic_service(AppModel *m, uint8_t duty, uint32_t now_ms);

// 고장을 기록하고 FAULT 화면으로 간다. 첫 원인만 남긴다.
void logic_fault(AppModel *m, Fault f, uint32_t now_ms);

// 매 틱 한 번. 샘플 유실·backlog 지속·상태값 오염을 검사해 FAULT 로 보낸다.
void logic_tick(AppModel *m, const SenseUpdate *sense, bool backlog, uint32_t now_ms);

// 이번 틱의 모터 duty. why 에는 그 이유("idle" / "run" / "service" / "fault")를 돌려준다.
uint8_t logic_output(const AppModel *m, const char **why);

// 모터가 돌 수 있는 화면인가(텔레메트리·SELFTEST 거부 판단용).
bool logic_is_running(const AppModel *m);

// LCD 두 줄을 만든다. 같은 모델이면 항상 같은 글자가 나온다.
void logic_render(const AppModel *m, ScreenLines out);

// 강도 → duty. 범위 밖이면 0.
uint8_t level_duty(Level lv);

const char *screen_name(Screen s);
const char *level_name(Level lv);
const char *fault_name(Fault f);

#endif  // APP_LOGIC_H
