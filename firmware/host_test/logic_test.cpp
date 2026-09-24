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

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond) do {                                                          \
    g_checks++;                                                                   \
    if (!(cond)) { g_fail++; printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static uint8_t out(const AppModel *m) { return logic_output(m, nullptr); }

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

static void test_breath_not_yet() {
    puts("  호흡 모드: 설정 화면까지만 (실행은 3단계)");
    AppModel m; uint32_t t = 0; logic_init(&m, t);
    press(&m, BTN_RIGHT, &t);                    // 커서 BREATH
    CHECK(m.cursor == 1);
    press(&m, BTN_OK, &t);
    CHECK(m.screen == SCR_BREATH_SETUP);
    press(&m, BTN_DOWN, &t);
    CHECK(!press(&m, BTN_OK, &t));               // START 는 아직 동작하지 않는다
    CHECK(m.screen == SCR_BREATH_SETUP);
    CHECK(out(&m) == 0);
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
                    ScreenLines L; logic_render(&m, L);
                    for (int r = 0; r < 2; r++)
                        for (int i = 0; i < LCD_COLS; i++)
                            if (!lcd_safe(L[r][i])) bad++;
                }
    CHECK(bad == 0);
}

int main() {
    puts("app_logic");
    test_boot_is_safe();
    test_normal_flow();
    test_setup_resets_level();
    test_back_and_cursor();
    test_breath_not_yet();
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
    test_lcd_charset();
    printf(g_fail ? "→ %d/%d 실패\n" : "→ %d개 검사 통과\n", g_fail ? g_fail : g_checks, g_checks);
    return g_fail ? 1 : 0;
}
