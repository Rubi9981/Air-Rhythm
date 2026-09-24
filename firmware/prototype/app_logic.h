// 화면·모드 상태 머신과 모터 출력 판단 — 순수 로직.
//
// Arduino / FreeRTOS / Serial 을 부르지 않는다. 시간은 인자(now_ms)로 받는다.
// 그래서 호스트에서 버튼 시나리오와 녹음 CSV 를 그대로 재생해 검증할 수 있다(host_test/logic.sh).
// 입출력(큐, motor_set, 검출기 초기화 요청, 메시지)은 task_app.cpp 가 맡는다.
//
// --- 지켜야 할 것 ---
//  1. 모터 출력은 logic_output() 한 곳에서만 정한다. 실행 화면이 아니면 0 이다.
//  2. FAULT 는 한 번 들어가면 나오는 길이 없다. 전원을 다시 켜야 풀린다.
//  3. 부팅 직후는 항상 모드 선택 화면 + 강도 LOW + 출력 0 이다. 이전 동작을 이어가지 않는다.
//  4. 호흡 모드는 "정착 완료 → 다음 호기 시작 이벤트" 를 본 뒤에만 실행에 들어가고,
//     신호를 잃으면 SIGNAL_LOST 에 머문다. 신호가 돌아와도 스스로 다시 시작하지 않는다.
//
// --- 화면 흐름 ---
//   MODE_SELECT ─OK→ NORMAL_SETUP ─START→ NORMAL_RUN ─OK(STOP)→ MODE_SELECT
//               ─OK→ BREATH_SETUP ─START→ BREATH_INIT ─정착→ BREATH_WAIT ─호기 시작→ BREATH_RUN
//                                          │ OK(취소)       │ OK(취소)       │ OK(STOP)
//                                          ▼                ▼                ▼
//                                     MODE_SELECT       MODE_SELECT      MODE_SELECT
//     BREATH_INIT ─시간 초과→ NO_SIGNAL      BREATH_WAIT ─무신호·10초 초과→ NO_SIGNAL
//     BREATH_RUN ─신호 유실→ SIGNAL_LOST     NO_SIGNAL / SIGNAL_LOST ─RETRY→ BREATH_INIT, ─BACK→ MODE_SELECT
//   어디서든 ─고장→ FAULT (끝)
//   서비스 빌드에서만: MODE_SELECT ─시리얼 DUTY n→ SERVICE ─STOP/OK/DUTY 0→ MODE_SELECT

#ifndef APP_LOGIC_H
#define APP_LOGIC_H

#include <stdint.h>

#include "app_types.h"

// 화면이 바뀐 직후 이 시간 동안은 버튼을 무시한다 — OK 를 빠르게 두 번 누른 것이
// 다음 화면으로 넘어가 곧바로 타진이 시작되는 일을 막는다.
// 단, 실행·초기화·대기 화면의 OK(= 정지·취소)는 예외다.
#define INPUT_LOCKOUT_MS     200

// 큐 backlog 가 이 틱 수만큼 이어지면 앱 태스크 과부하로 보고 FAULT (50틱 = 1초).
#define BACKLOG_FAULT_TICKS  50

// 호흡 모드 시간 한도.
// 정착은 검출기가 샘플 수(SETTLE_S = 12초)로 판정한다. 아래 INIT 한도는 그 판정을 대신하지
// 않는다 — 정착 신호가 오지 않는 이상 상황에서 화면이 영영 멈추지 않게 하는 안전망일 뿐이다.
#define BREATH_INIT_TIMEOUT_MS  20000   // 초기화 요청부터 정착 완료까지
#define BREATH_WAIT_TIMEOUT_MS  10000   // 정착 후 첫 호기 시작까지 (분당 12회 기준 약 두 번)

typedef enum {
    SCR_MODE_SELECT = 0,
    SCR_NORMAL_SETUP,
    SCR_NORMAL_RUN,
    SCR_BREATH_SETUP,
    SCR_BREATH_INIT,        // 검출기 초기화 → 정착 대기 ("Detecting breath")
    SCR_BREATH_WAIT,        // 정착 완료, 다음 호기 시작 대기 ("Wait for EXHALE")
    SCR_BREATH_RUN,         // 호기에만 타진
    SCR_NO_SIGNAL,          // 정착 후 호흡을 못 잡음 — RETRY / BACK
    SCR_SIGNAL_LOST,        // 실행 중 신호 유실 — RETRY / BACK
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
// NO_SIGNAL / SIGNAL_LOST 화면의 커서 위치
enum { CHOICE_RETRY = 0, CHOICE_BACK, CHOICE_ITEMS };

typedef struct {
    Screen   screen;
    uint8_t  cursor;           // 화면 안의 선택 위치
    Level    level;            // 타진 강도
    Fault    fault;            // FAULT_NONE 이 아니면 screen == SCR_FAULT
    uint32_t screen_since_ms;  // 이 화면에 들어온 시각. 입력 잠금·호흡 시간 한도 기준
    uint16_t backlog_ticks;    // backlog 가 연속된 틱 수
    uint8_t  service_duty;     // SCR_SERVICE 에서만 의미가 있다

    // --- 호흡 모드 ---
    bool     reset_wanted;     // 검출기 초기화를 요청해야 한다 (task_app 이 보고 처리)
    uint16_t sense_epoch;      // 요청한 초기화 회차. 이와 다른 샘플은 초기화 전의 것이라 믿지 않는다
    int8_t   phase;            // 표시용: 마지막 샘플의 위상 (BR_RISING / BR_FALLING / BR_UNKNOWN)
    float    bpm;              // 표시용: 마지막 샘플의 호흡수. 0 이면 아직 모름
} AppModel;

#define LCD_COLS 16
typedef char ScreenLines[2][LCD_COLS + 1];   // LCD 두 줄. 각 줄은 정확히 16자 + '\0'

void logic_init(AppModel *m, uint32_t now_ms);

// 버튼 하나를 반영한다. 잠금·FAULT 로 무시했으면 false.
bool logic_button(AppModel *m, Button b, uint32_t now_ms);

// 원격 정지(시리얼 STOP). 실행·초기화·대기 중이면 멈추고 모드 선택으로 간다. FAULT 는 그대로 둔다.
void logic_stop(AppModel *m, uint32_t now_ms);

// 서비스 모드 직접 구동. duty 0 은 정지. 모드 선택·서비스 화면에서만 받는다(아니면 false).
// SERVICE_MODE 빌드인지는 호출자가 확인한다.
bool logic_service(AppModel *m, uint8_t duty, uint32_t now_ms);

// 고장을 기록하고 FAULT 화면으로 간다. 첫 원인만 남긴다.
void logic_fault(AppModel *m, Fault f, uint32_t now_ms);

// 샘플 하나마다 한 번. 고장 검사(샘플 유실·backlog 지속·상태값 오염)와
// 호흡 모드 전환(정착 → 호기 대기 → 실행, 신호 유실)을 한다.
// 이벤트를 놓치지 않도록 backlog 로 밀린 샘플도 순서대로 전부 넣어야 한다.
void logic_tick(AppModel *m, const SenseUpdate *sense, bool backlog, uint32_t now_ms);

// 검출기 초기화 요청 — logic 은 하드웨어를 부르지 않으므로 task_app 이 대신 요청한다.
//   if (logic_reset_wanted(&m)) logic_reset_issued(&m, sense_request_reset());
bool logic_reset_wanted(const AppModel *m);
void logic_reset_issued(AppModel *m, uint16_t epoch);

// 이번 틱의 모터 duty. sense 는 지금 든(가장 최근) 샘플, backlog 는 큐에 더 밀려 있는지.
// why 에는 그 이유를 돌려준다:
//   "idle" / "run" / "service" / "fault" / "init" / "wait"
//   호흡 실행 중 멈춘 이유: "backlog" / "reset" / "settling" / "nosig" / "inhale"
uint8_t logic_output(const AppModel *m, const SenseUpdate *sense, bool backlog, const char **why);

// 모터가 돌 수 있는 화면인가(텔레메트리·SELFTEST 거부 판단용).
bool logic_is_running(const AppModel *m);

// 호흡 모드 흐름(초기화·대기·실행·무신호·신호 유실) 안에 있는가.
bool logic_in_breath_flow(const AppModel *m);

// LCD 두 줄을 만든다. 같은 모델이면 항상 같은 글자가 나온다.
void logic_render(const AppModel *m, ScreenLines out);

// 강도 → duty. 범위 밖이면 0.
uint8_t level_duty(Level lv);

const char *screen_name(Screen s);
const char *level_name(Level lv);
const char *fault_name(Fault f);

#endif  // APP_LOGIC_H
