// app_logic 단위 테스트 — 화면 흐름과 모터 출력 규칙을 버튼 시나리오로 검증한다.
//
// 모터를 실제로 돌리지 않고도 "언제 돌고 언제 서는가" 를 확인하는 유일한 자동 검사다.
// 목표 동작 문서의 안전 규칙 하나하나가 아래 테스트 하나씩에 대응한다.
//
//   sh firmware/host_test/logic.sh

#include <stdio.h>
#include <string.h>

#include "app_logic.h"
#include "board_config.h"
#include "breath_slope.h"

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond) do {                                                          \
    g_checks++;                                                                   \
    if (!(cond)) { g_fail++; printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static uint8_t out(const AppModel *m, const SenseUpdate *s = nullptr, bool backlog = false) {
    return logic_output(m, s, backlog, nullptr);
}

// 잠금 시간이 지난 뒤에 누른다. t 는 누를 때마다 앞으로 간다.
static bool press(AppModel *m, Button b, uint32_t *t) {
    *t += INPUT_LOCKOUT_MS + 10;
    return logic_button(m, b, *t);
}

// 모드 선택에서 일반 모드 실행까지. 끝나면 NORMAL_RUN / LOW.
static void to_normal_run(AppModel *m, uint32_t *t) {
    press(m, BTN_OK, t);        // NORMAL 선택 → NORMAL_SETUP
    press(m, BTN_DOWN, t);      // 커서 START
    press(m, BTN_OK, t);        // → NORMAL_RUN
}

static void test_boot_is_safe() {
    puts("  부팅 직후: 모드 선택 / LOW / 출력 0");
    AppModel m; logic_init(&m, 1000);
    CHECK(m.screen == SCR_MODE_SELECT);
    CHECK(m.cursor == 0);                        // 초기 선택은 일반 모드 (목표 2.2)
    CHECK(m.level == LEVEL_LOW);
    CHECK(m.fault == FAULT_NONE);
    CHECK(out(&m) == 0);
}

static void test_normal_flow() {
    puts("  일반 모드: 설정 → 실행 → 강도 변경 → 정지");
    AppModel m; uint32_t t = 0; logic_init(&m, t);

    CHECK(press(&m, BTN_OK, &t));
    CHECK(m.screen == SCR_NORMAL_SETUP);
    CHECK(out(&m) == 0);                         // 설정 화면에서는 돌지 않는다

    CHECK(press(&m, BTN_RIGHT, &t));             // 설정 화면에서 강도 변경
    CHECK(m.level == LEVEL_MID);
    CHECK(press(&m, BTN_DOWN, &t));              // START
    CHECK(press(&m, BTN_OK, &t));
    CHECK(m.screen == SCR_NORMAL_RUN);
    CHECK(out(&m) == DUTY_MID);

    CHECK(press(&m, BTN_UP, &t));                // 실행 중 강도 변경 (목표 3.2.3)
    CHECK(out(&m) == DUTY_HIGH);
    CHECK(press(&m, BTN_UP, &t));                // HIGH 에서 더 올라가지 않는다
    CHECK(out(&m) == DUTY_HIGH);
    CHECK(press(&m, BTN_LEFT, &t));
    CHECK(press(&m, BTN_DOWN, &t));
    CHECK(press(&m, BTN_DOWN, &t));              // LOW 에서 더 내려가지 않는다
    CHECK(out(&m) == DUTY_LOW);

    CHECK(press(&m, BTN_OK, &t));                // 타진 정지 → 모드 선택 (목표 3.2.5~6)
    CHECK(m.screen == SCR_MODE_SELECT);
    CHECK(out(&m) == 0);
}

static void test_setup_resets_level() {
    puts("  설정 화면에 들어갈 때마다 강도는 LOW");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    to_normal_run(&m, &t);
    press(&m, BTN_UP, &t); press(&m, BTN_UP, &t);
    CHECK(m.level == LEVEL_HIGH);
    press(&m, BTN_OK, &t);                       // 정지
    press(&m, BTN_OK, &t);                       // 다시 NORMAL
    CHECK(m.screen == SCR_NORMAL_SETUP);
    CHECK(m.level == LEVEL_LOW);
}

static void test_back_and_cursor() {
    puts("  설정 화면: 커서 순환, BACK, POWER 위의 OK");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    press(&m, BTN_OK, &t);
    CHECK(m.cursor == SETUP_POWER);
    CHECK(!press(&m, BTN_OK, &t));               // POWER 위에서 OK 는 무시
    CHECK(m.screen == SCR_NORMAL_SETUP);
    press(&m, BTN_UP, &t);                       // 위로 순환 → BACK
    CHECK(m.cursor == SETUP_BACK);
    press(&m, BTN_DOWN, &t);                     // 아래로 순환 → POWER
    CHECK(m.cursor == SETUP_POWER);
    press(&m, BTN_UP, &t);
    CHECK(press(&m, BTN_OK, &t));                // BACK
    CHECK(m.screen == SCR_MODE_SELECT);
}

// ---------------------------------------------------------------------------
// 호흡 모드 — 센서 샘플을 직접 만들어 넣는다
// ---------------------------------------------------------------------------

// 샘플 하나. 기본값은 "정착 완료 + 신호 정상 + 위상 모름"
static SenseUpdate smp(uint16_t epoch, int8_t phase = BR_UNKNOWN, uint8_t events = 0,
                       bool settled = true, bool sig = true, float bpm = 0.0f) {
    SenseUpdate s = {};
    s.epoch  = epoch;
    s.phase  = phase;
    s.events = events;
    s.flags  = (uint8_t)((settled ? FLAG_SETTLED : 0) | (sig ? FLAG_SIGNAL_OK : 0));
    s.bpm    = bpm;
    return s;
}

// 같은 샘플을 n 개(20ms 간격) 넣는다. 이벤트는 첫 샘플에만 싣는다. 마지막 출력을 돌려준다.
static uint8_t feed(AppModel *m, SenseUpdate s, int n, uint32_t *t, bool backlog = false) {
    uint8_t o = 0;
    for (int i = 0; i < n; i++) {
        *t += 20;
        logic_tick(m, &s, backlog, *t);
        o = out(m, &s, backlog);
        s.events = 0;
    }
    return o;
}

// 모드 선택 → 호흡 모드 → START. 끝나면 BREATH_INIT, 초기화 요청 대기 중.
static void to_breath_init(AppModel *m, uint32_t *t) {
    press(m, BTN_DOWN, t);      // 커서 BREATH
    press(m, BTN_OK, t);        // BREATH_SETUP
    press(m, BTN_DOWN, t);      // START
    press(m, BTN_OK, t);        // → BREATH_INIT
}

// START 부터 정착까지. 끝나면 BREATH_WAIT, 검출기 회차 = epoch.
static void to_breath_wait(AppModel *m, uint32_t *t, uint16_t epoch = 1) {
    to_breath_init(m, t);
    logic_reset_issued(m, epoch);
    feed(m, smp(epoch, BR_UNKNOWN, 0, false), 10, t);   // 정착 중
    feed(m, smp(epoch), 1, t);                          // 정착 완료
}

// WAIT 에서 호기 시작 이벤트로 실행까지.
static void to_breath_run(AppModel *m, uint32_t *t, uint16_t epoch = 1) {
    to_breath_wait(m, t, epoch);
    feed(m, smp(epoch, BR_FALLING, EVENT_EXHALE), 1, t);
}

static void test_breath_init() {
    puts("  호흡 초기화: 검출기 초기화를 요청하고, 그 회차가 '정착 완료' 여야만 넘어간다 (4.2)");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    to_breath_init(&m, &t);
    CHECK(m.screen == SCR_BREATH_INIT);
    CHECK(logic_reset_wanted(&m));
    CHECK(out(&m) == 0);

    // 요청을 보내기 전: 이전 회차의 "정착·호기" 샘플이 와도 넘어가지 않는다
    feed(&m, smp(0, BR_FALLING, EVENT_EXHALE), 5, &t);
    CHECK(m.screen == SCR_BREATH_INIT);

    logic_reset_issued(&m, 3);
    CHECK(!logic_reset_wanted(&m));
    feed(&m, smp(2, BR_FALLING), 5, &t);        // 큐에 남아 있던 옛 회차 — 무시
    CHECK(m.screen == SCR_BREATH_INIT);
    CHECK(feed(&m, smp(3, BR_FALLING, 0, false), 599, &t) == 0);   // 새 회차, 정착 중 (12초)
    CHECK(m.screen == SCR_BREATH_INIT);

    // 정착 완료 샘플에 호기 시작이 같이 실려 와도 곧바로 실행하지 않는다 — 대기로만 간다
    CHECK(feed(&m, smp(3, BR_FALLING, EVENT_EXHALE), 1, &t) == 0);
    CHECK(m.screen == SCR_BREATH_WAIT);
}

static void test_breath_waits_for_next_exhale() {
    puts("  호흡 대기: 이미 호기 중이어도 켜지 않고, 다음 호기 시작 이벤트에서 시작 (4.3.3~4)");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    to_breath_wait(&m, &t);
    CHECK(feed(&m, smp(1, BR_FALLING), 150, &t) == 0);            // 호기 중 3초 — 그래도 대기
    CHECK(m.screen == SCR_BREATH_WAIT);
    CHECK(feed(&m, smp(1, BR_RISING, EVENT_INHALE), 100, &t) == 0);
    CHECK(m.screen == SCR_BREATH_WAIT);
    CHECK(feed(&m, smp(1, BR_FALLING, EVENT_EXHALE), 1, &t) == DUTY_LOW);   // 그 샘플부터 타진
    CHECK(m.screen == SCR_BREATH_RUN);
}

static void test_breath_run_gate() {
    puts("  호흡 실행: 호기에만 출력, 흡기·backlog·옛 회차·무신호는 0 (4.4.2)");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    to_breath_run(&m, &t);
    const char *why = "";
    SenseUpdate s;

    s = smp(1, BR_FALLING); CHECK(logic_output(&m, &s, false, &why) == DUTY_LOW); CHECK(!strcmp(why, "run"));
    s = smp(1, BR_RISING);  CHECK(logic_output(&m, &s, false, &why) == 0);        CHECK(!strcmp(why, "inhale"));
    s = smp(1, BR_UNKNOWN); CHECK(logic_output(&m, &s, false, &why) == 0);        CHECK(!strcmp(why, "inhale"));
    s = smp(1, BR_FALLING); CHECK(logic_output(&m, &s, true,  &why) == 0);        CHECK(!strcmp(why, "backlog"));
    s = smp(9, BR_FALLING); CHECK(logic_output(&m, &s, false, &why) == 0);        CHECK(!strcmp(why, "reset"));
    s = smp(1, BR_FALLING, 0, false); CHECK(logic_output(&m, &s, false, &why) == 0); CHECK(!strcmp(why, "settling"));
    CHECK(logic_output(&m, nullptr, false, &why) == 0);

    // backlog 몇 틱은 멈춤만 하고 실행 화면은 유지한다
    CHECK(feed(&m, smp(1, BR_FALLING), 5, &t, true) == 0);
    CHECK(m.screen == SCR_BREATH_RUN);
    CHECK(feed(&m, smp(1, BR_FALLING), 1, &t) == DUTY_LOW);

    // 실행 중 강도 변경
    press(&m, BTN_UP, &t);
    s = smp(1, BR_FALLING); CHECK(out(&m, &s) == DUTY_MID);
}

static void test_breath_no_signal() {
    puts("  호흡 대기: 10초 안에 호기 시작이 없거나 무신호면 NO BREATH SIGNAL (4.3.5)");
    // 시간 초과
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    to_breath_wait(&m, &t);
    feed(&m, smp(1, BR_RISING), BREATH_WAIT_TIMEOUT_MS / 20 - 1, &t);
    CHECK(m.screen == SCR_BREATH_WAIT);
    feed(&m, smp(1, BR_RISING), 1, &t);
    CHECK(m.screen == SCR_NO_SIGNAL);
    CHECK(out(&m) == 0);

    // NOSIG 이벤트
    logic_init(&m, t); to_breath_wait(&m, &t);
    feed(&m, smp(1, BR_UNKNOWN, EVENT_SIGNAL_LOST, true, false), 1, &t);
    CHECK(m.screen == SCR_NO_SIGNAL);

    // 신호 플래그만 꺼진 경우
    logic_init(&m, t); to_breath_wait(&m, &t);
    feed(&m, smp(1, BR_UNKNOWN, 0, true, false), 1, &t);
    CHECK(m.screen == SCR_NO_SIGNAL);
}

static void test_breath_init_timeout() {
    puts("  호흡 초기화: 정착 신호가 20초 안에 안 오면 NO BREATH SIGNAL (멈춰 있지 않게)");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    to_breath_init(&m, &t);
    logic_reset_issued(&m, 1);
    feed(&m, smp(1, BR_UNKNOWN, 0, false), BREATH_INIT_TIMEOUT_MS / 20 + 1, &t);
    CHECK(m.screen == SCR_NO_SIGNAL);
}

static void test_breath_retry_back() {
    puts("  NO SIGNAL / SIGNAL LOST: RETRY 는 초기화부터, BACK 은 모드 선택 (4.3.6 / 4.5.4)");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    to_breath_wait(&m, &t, 4);
    feed(&m, smp(4, BR_UNKNOWN, EVENT_SIGNAL_LOST, true, false), 1, &t);
    CHECK(m.cursor == CHOICE_RETRY);
    press(&m, BTN_UP, &t);
    press(&m, BTN_UP, &t);                       // 두 번 토글 → 다시 RETRY
    CHECK(press(&m, BTN_OK, &t));
    CHECK(m.screen == SCR_BREATH_INIT);
    CHECK(logic_reset_wanted(&m));               // 12초 초기화를 다시 한다
    CHECK(m.phase == BR_UNKNOWN);                // 이전 회차의 위상은 화면에서 지운다

    logic_reset_issued(&m, 5);
    feed(&m, smp(5), 1, &t);
    feed(&m, smp(5, BR_UNKNOWN, EVENT_SIGNAL_LOST, true, false), 1, &t);
    CHECK(m.screen == SCR_NO_SIGNAL);
    press(&m, BTN_RIGHT, &t);                    // BACK
    CHECK(press(&m, BTN_OK, &t));
    CHECK(m.screen == SCR_MODE_SELECT);
}

static void test_breath_signal_lost_latches() {
    puts("  실행 중 신호 유실: 즉시 정지, 신호가 돌아와도 다시 켜지 않는다 (4.5)");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    to_breath_run(&m, &t);
    CHECK(feed(&m, smp(1, BR_FALLING, EVENT_SIGNAL_LOST, true, false), 1, &t) == 0);
    CHECK(m.screen == SCR_SIGNAL_LOST);
    // 10초 동안 멀쩡한 호흡이 다시 들어와도 그대로다
    for (int i = 0; i < 5; i++) {
        CHECK(feed(&m, smp(1, BR_FALLING, EVENT_EXHALE), 50, &t) == 0);
        feed(&m, smp(1, BR_RISING, EVENT_INHALE), 50, &t);
    }
    CHECK(m.screen == SCR_SIGNAL_LOST);

    // 플래그만 꺼져도 같다
    logic_init(&m, t); to_breath_run(&m, &t);
    CHECK(feed(&m, smp(1, BR_FALLING, 0, true, false), 1, &t) == 0);
    CHECK(m.screen == SCR_SIGNAL_LOST);
}

static void test_breath_cancel_and_stop() {
    puts("  호흡 초기화·대기 중 OK 는 취소(잠금 예외), 실행 중 OK·원격 STOP 은 정지 (4.2.9)");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    to_breath_init(&m, &t);
    CHECK(logic_button(&m, BTN_OK, t + 10));     // 화면 전환 직후라도 받는다
    CHECK(m.screen == SCR_MODE_SELECT);
    CHECK(!logic_reset_wanted(&m));              // 보내기 전에 취소했으면 요청도 거둔다

    logic_init(&m, t); to_breath_init(&m, &t);
    CHECK(!press(&m, BTN_UP, &t));               // 초기화 중에는 OK 말고는 받지 않는다
    logic_init(&m, t); to_breath_wait(&m, &t);
    CHECK(!press(&m, BTN_RIGHT, &t));
    CHECK(press(&m, BTN_OK, &t));
    CHECK(m.screen == SCR_MODE_SELECT);

    logic_init(&m, t); to_breath_run(&m, &t);
    CHECK(logic_button(&m, BTN_OK, t + 10));
    CHECK(m.screen == SCR_MODE_SELECT);

    logic_init(&m, t); to_breath_init(&m, &t); logic_stop(&m, t);
    CHECK(m.screen == SCR_MODE_SELECT);
    logic_init(&m, t); to_breath_run(&m, &t); logic_stop(&m, t);
    CHECK(m.screen == SCR_MODE_SELECT);
    logic_init(&m, t); to_breath_wait(&m, &t);
    feed(&m, smp(1, BR_UNKNOWN, EVENT_SIGNAL_LOST, true, false), 1, &t);
    logic_stop(&m, t);
    CHECK(m.screen == SCR_NO_SIGNAL);            // 이미 멈춰 있는 화면은 그대로
}

static void test_breath_fault() {
    puts("  호흡 실행 중 샘플 유실 → FAULT");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    to_breath_run(&m, &t);
    SenseUpdate s = smp(1, BR_FALLING);
    s.drops = 3;
    logic_tick(&m, &s, false, t += 20);
    CHECK(m.screen == SCR_FAULT);
    CHECK(out(&m, &s) == 0);
}

static void test_input_lockout() {
    puts("  화면 전환 직후 200ms 입력 무시 — OK 두 번이 곧바로 시작으로 이어지지 않는다");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    t = INPUT_LOCKOUT_MS + 10;
    CHECK(logic_button(&m, BTN_OK, t));          // NORMAL_SETUP
    CHECK(!logic_button(&m, BTN_DOWN, t + 50));  // 잠금 중
    CHECK(m.cursor == SETUP_POWER);
    CHECK(logic_button(&m, BTN_DOWN, t + INPUT_LOCKOUT_MS));   // 정확히 200ms 뒤부터 받는다

    // START 를 누른 직후의 OK 는 "정지" 다 — 잠금 예외. 결과는 즉시 정지(안전한 쪽).
    t += 1000;
    CHECK(logic_button(&m, BTN_OK, t));
    CHECK(m.screen == SCR_NORMAL_RUN);
    CHECK(!logic_button(&m, BTN_UP, t + 50));    // 강도 변경은 잠금을 받는다
    CHECK(m.level == LEVEL_LOW);
    CHECK(logic_button(&m, BTN_OK, t + 50));     // 정지는 잠금을 받지 않는다 (목표 5.6)
    CHECK(m.screen == SCR_MODE_SELECT);
    CHECK(out(&m) == 0);
}

static void test_remote_stop() {
    puts("  원격 STOP: 실행 중이면 정지, 아니면 아무 일 없음");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    press(&m, BTN_OK, &t);
    logic_stop(&m, t);
    CHECK(m.screen == SCR_NORMAL_SETUP);         // 실행 중이 아니면 화면을 바꾸지 않는다
    logic_init(&m, t);
    to_normal_run(&m, &t);
    CHECK(out(&m) == DUTY_LOW);
    logic_stop(&m, t + 1);                       // 잠금과 무관
    CHECK(m.screen == SCR_MODE_SELECT);
    CHECK(out(&m) == 0);
}

static void test_fault_latches() {
    puts("  FAULT: 출력 0, 모든 입력 무시, 풀리지 않음");
    const Fault kinds[] = { FAULT_SENSE_STALL, FAULT_SENSE_OVERLOAD, FAULT_BAD_STATE };
    for (Fault f : kinds) {
        AppModel m; uint32_t t = 0; logic_init(&m, t);
        to_normal_run(&m, &t);
        CHECK(out(&m) > 0);
        logic_fault(&m, f, t);
        CHECK(m.screen == SCR_FAULT);
        CHECK(m.fault == f);
        CHECK(out(&m) == 0);
        for (int b = 0; b < BTN_COUNT; b++) CHECK(!press(&m, (Button)b, &t));
        logic_stop(&m, t);
        CHECK(!logic_service(&m, 100, t));
        SenseUpdate ok = {};
        for (int i = 0; i < 500; i++) logic_tick(&m, &ok, false, t += 20);   // 10초 정상이어도
        CHECK(m.screen == SCR_FAULT);                                        // 그대로다
        CHECK(out(&m) == 0);
    }
}

static void test_first_fault_kept() {
    puts("  FAULT: 첫 원인만 남긴다");
    AppModel m; logic_init(&m, 0);
    logic_fault(&m, FAULT_SENSE_STALL, 0);
    logic_fault(&m, FAULT_SENSE_OVERLOAD, 10);
    CHECK(m.fault == FAULT_SENSE_STALL);
}

static void test_drops_fault() {
    puts("  샘플 유실(QDROP) → FAULT, 모드와 무관");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    SenseUpdate s = {};
    s.drops = 1;
    logic_tick(&m, &s, false, t);                // 메뉴 화면에서도
    CHECK(m.fault == FAULT_SENSE_OVERLOAD);
}

static void test_backlog_fault() {
    puts("  backlog: 1초 미만은 무시, 1초 지속이면 FAULT");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    to_normal_run(&m, &t);
    SenseUpdate s = {};
    for (int i = 0; i < BACKLOG_FAULT_TICKS - 1; i++) logic_tick(&m, &s, true, t += 20);
    CHECK(m.fault == FAULT_NONE);
    CHECK(out(&m) == DUTY_LOW);                  // 일반 모드는 backlog 로 끊기지 않는다
    logic_tick(&m, &s, false, t += 20);          // 한 틱 풀리면 다시 센다
    for (int i = 0; i < BACKLOG_FAULT_TICKS - 1; i++) logic_tick(&m, &s, true, t += 20);
    CHECK(m.fault == FAULT_NONE);
    logic_tick(&m, &s, true, t += 20);
    CHECK(m.fault == FAULT_SENSE_OVERLOAD);
    CHECK(out(&m) == 0);
}

static void test_bad_state_fault() {
    puts("  상태값 오염 → FAULT, 그 틱의 출력도 0 (목표 6.4 / 8.6)");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    to_normal_run(&m, &t);
    m.level = (Level)7;                          // 메모리 오염 흉내
    CHECK(out(&m) == 0);                         // 판단 즉시 0
    logic_tick(&m, nullptr, false, t);
    CHECK(m.fault == FAULT_BAD_STATE);

    logic_init(&m, t);
    m.screen = (Screen)42;
    CHECK(out(&m) == 0);
    logic_tick(&m, nullptr, false, t);
    CHECK(m.fault == FAULT_BAD_STATE);

    logic_init(&m, t);
    m.screen = SCR_FAULT;                        // 원인 없는 FAULT 화면도 오염이다
    logic_tick(&m, nullptr, false, t);
    CHECK(m.fault == FAULT_BAD_STATE);
}

static void test_service() {
    puts("  서비스 모드: 모드 선택에서만 진입, OK·STOP·DUTY 0 으로 정지");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    CHECK(logic_service(&m, 90, t));
    CHECK(m.screen == SCR_SERVICE);
    CHECK(out(&m) == 90);
    CHECK(logic_service(&m, 120, t + 1));        // 서비스 화면에서 값 변경
    CHECK(out(&m) == 120);
    CHECK(logic_service(&m, 0, t + 2));
    CHECK(m.screen == SCR_MODE_SELECT);
    CHECK(out(&m) == 0);

    logic_service(&m, 90, t);
    CHECK(logic_button(&m, BTN_OK, t + 1));      // 잠금 예외 — 정지
    CHECK(out(&m) == 0);

    logic_service(&m, 90, t);
    logic_stop(&m, t);
    CHECK(out(&m) == 0);

    to_normal_run(&m, &t);
    CHECK(!logic_service(&m, 90, t));            // 실행 중에는 끼어들 수 없다
    CHECK(out(&m) == DUTY_LOW);
}

static void test_level_only_on_power() {
    puts("  설정 화면: 좌/우는 POWER 항목이 보일 때만 강도를 바꾼다");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    press(&m, BTN_OK, &t);
    press(&m, BTN_DOWN, &t);                     // START 항목
    CHECK(!press(&m, BTN_RIGHT, &t));
    CHECK(!press(&m, BTN_LEFT, &t));
    CHECK(m.level == LEVEL_LOW);
    press(&m, BTN_UP, &t);                       // POWER 항목
    CHECK(press(&m, BTN_RIGHT, &t));
    CHECK(m.level == LEVEL_MID);
}

static void test_render() {
    puts("  화면: 모든 줄이 정확히 16자, 주요 문구");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    ScreenLines L;

    auto show = [&](const char *l0, const char *l1) {
        logic_render(&m, L);
        CHECK(strlen(L[0]) == 16); CHECK(strlen(L[1]) == 16);
        if (strcmp(L[0], l0) != 0 || strcmp(L[1], l1) != 0) {
            g_fail++;
            printf("    FAIL 화면 |%s|%s|  기대 |%s|%s|\n", L[0], L[1], l0, l1);
        }
        g_checks++;
    };

    show("SELECT MODE  1/2", "> NORMAL        ");
    press(&m, BTN_DOWN, &t);
    show("SELECT MODE  2/2", "> BREATH        ");
    press(&m, BTN_UP, &t);

    press(&m, BTN_OK, &t);
    show("NORMAL MODE  1/3", "POWER   < LOW  >");
    press(&m, BTN_RIGHT, &t); press(&m, BTN_RIGHT, &t);
    show("NORMAL MODE  1/3", "POWER   < HIGH >");
    press(&m, BTN_DOWN, &t);
    show("NORMAL MODE  2/3", "> START         ");
    press(&m, BTN_DOWN, &t);
    show("NORMAL MODE  3/3", "> BACK          ");
    press(&m, BTN_DOWN, &t);                     // 순환 → POWER
    press(&m, BTN_DOWN, &t);                     // START

    press(&m, BTN_OK, &t);
    show("NORMAL       RUN", "POWER:HIGH >STOP");
    press(&m, BTN_DOWN, &t);
    show("NORMAL       RUN", "POWER:MID  >STOP");

    logic_fault(&m, FAULT_SENSE_STALL, t);
    show("SENSOR FAULT    ", "POWER OFF & ON  ");

    logic_init(&m, t); logic_service(&m, 7, t);
    show("SERVICE MODE    ", "DUTY:7     >STOP");

    logic_init(&m, t);
    press(&m, BTN_DOWN, &t); press(&m, BTN_OK, &t);
    show("BREATH MODE  1/3", "POWER   < LOW  >");
    press(&m, BTN_DOWN, &t);
    show("Wear belt first ", "> START         ");   // 시작 직전 착용 안내
    press(&m, BTN_DOWN, &t);
    show("BREATH MODE  3/3", "> BACK          ");
}

static void test_render_breath() {
    puts("  화면: 호흡 모드 (초기화 · 대기 · 실행 · 무신호 · 신호 유실)");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    ScreenLines L;
    auto show = [&](const char *l0, const char *l1) {
        logic_render(&m, L);
        g_checks++;
        if (strcmp(L[0], l0) != 0 || strcmp(L[1], l1) != 0) {
            g_fail++;
            printf("    FAIL 화면 |%s|%s|  기대 |%s|%s|\n", L[0], L[1], l0, l1);
        }
    };

    to_breath_init(&m, &t);
    show("Detecting breath", "Please Wait...  ");
    logic_reset_issued(&m, 1);
    feed(&m, smp(1), 1, &t);
    show("Breath detected ", "Wait for EXHALE ");
    feed(&m, smp(1, BR_FALLING, EVENT_EXHALE, true, true, 14.6f), 1, &t);
    show("SYNC EXH    14.6", "LOW  RUN   >STOP");
    feed(&m, smp(1, BR_RISING, EVENT_INHALE, true, true, 9.0f), 1, &t);
    show("SYNC INH     9.0", "LOW  WAIT  >STOP");
    press(&m, BTN_UP, &t); press(&m, BTN_UP, &t);
    feed(&m, smp(1, BR_UNKNOWN), 1, &t);
    show("SYNC ---    --.-", "HIGH WAIT  >STOP");
    feed(&m, smp(1, BR_UNKNOWN, EVENT_SIGNAL_LOST, true, false), 1, &t);
    show("SIGNAL LOST     ", ">RETRY      BACK");
    press(&m, BTN_DOWN, &t);
    show("SIGNAL LOST     ", " RETRY     >BACK");

    logic_init(&m, t); to_breath_wait(&m, &t);
    feed(&m, smp(1, BR_UNKNOWN, EVENT_SIGNAL_LOST, true, false), 1, &t);
    show("NO BREATH SIGNAL", ">RETRY      BACK");
}

// HD44780 문자 ROM(A00, 한국·일본 판매 모듈 대부분)에서 ASCII 와 모양이 같은 글자만 쓴다.
// 0x5C '\' 는 엔화 기호로, 0x7E '~' 는 → 로, 0x7F 는 ← 로 나온다. 한글은 아예 없다.
static bool lcd_safe(char c) {
    const unsigned char u = (unsigned char)c;
    return u >= 0x20 && u <= 0x7D && u != 0x5C;
}

static void test_lcd_charset() {
    puts("  화면: 모든 상태에서 LCD 문자 ROM 에 있는 글자만 쓴다");
    const Fault faults[] = { FAULT_SENSE_STALL, FAULT_SENSE_OVERLOAD, FAULT_BAD_STATE };
    int bad = 0;
    for (int s = 0; s < SCR_COUNT; s++)
        for (int c = 0; c < SETUP_ITEMS; c++)
            for (int lv = 0; lv < LEVEL_COUNT; lv++)
                for (Fault f : faults) {
                    AppModel m; logic_init(&m, 0);
                    m.screen = (Screen)s; m.cursor = (uint8_t)c; m.level = (Level)lv;
                    m.fault = f; m.service_duty = 255;
                    const int8_t phases[] = { BR_RISING, BR_FALLING, BR_UNKNOWN };
                    const float  bpms[]   = { 0.0f, 5.1f, 14.6f, 43.0f };
                    for (int8_t ph : phases)
                        for (float bpm : bpms) {
                            m.phase = ph; m.bpm = bpm;
                            ScreenLines L; logic_render(&m, L);
                            for (int r = 0; r < 2; r++) {
                                if (strlen(L[r]) != LCD_COLS) bad++;
                                for (int i = 0; i < LCD_COLS; i++)
                                    if (!lcd_safe(L[r][i])) bad++;
                            }
                        }
                }
    CHECK(bad == 0);
}

int main() {
    puts("app_logic");
    test_boot_is_safe();
    test_normal_flow();
    test_setup_resets_level();
    test_back_and_cursor();
    test_breath_init();
    test_breath_waits_for_next_exhale();
    test_breath_run_gate();
    test_breath_no_signal();
    test_breath_init_timeout();
    test_breath_retry_back();
    test_breath_signal_lost_latches();
    test_breath_cancel_and_stop();
    test_breath_fault();
    test_input_lockout();
    test_remote_stop();
    test_fault_latches();
    test_first_fault_kept();
    test_drops_fault();
    test_backlog_fault();
    test_bad_state_fault();
    test_service();
    test_level_only_on_power();
    test_render();
    test_render_breath();
    test_lcd_charset();
    printf(g_fail ? "→ %d/%d 실패\n" : "→ %d개 검사 통과\n", g_fail ? g_fail : g_checks, g_checks);
    return g_fail ? 1 : 0;
}
