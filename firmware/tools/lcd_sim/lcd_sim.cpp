// LCD1602 + 5방향 스위치 시뮬레이터 — 보드 없이 PC 터미널에서 화면 흐름을 확인한다.
//
// 펌웨어와 같은 app_logic.cpp 를 그대로 컴파일한다. 화면 문구·입력 잠금·FAULT 규칙이
// 실제 기기와 한 글자도 다르지 않다. 다른 것은 입력(키보드)과 출력(터미널)뿐이다.
//
//   sh firmware/tools/lcd_sim/run.sh
//
// 키:  ↑ ↓ ← →  방향 버튼      Enter / Space  OK(가운데)
//      s  원격 STOP            f  센서 고장 흉내      r  전원 재인가      q  종료
//
// 표준입력이 터미널이 아니면(파이프) 키 목록을 읽어 한 키마다 화면을 찍는다 — 자동 확인용.
//   printf 'OK D OK U\n' | ./lcd_sim        (토큰: U D L R OK S F P)

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "app_logic.h"
#include "board_config.h"

static uint32_t now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

// ---------------------------------------------------------------------------
// 입력 → 모델
// ---------------------------------------------------------------------------

typedef enum { K_NONE, K_BTN, K_STOP, K_FAULT, K_POWER, K_QUIT } KeyKind;
struct Key { KeyKind kind; Button btn; };

static char s_last[48] = "";

static void apply_key(AppModel *m, Key k, uint32_t t) {
    switch (k.kind) {
        case K_BTN: {
            const bool ok = logic_button(m, k.btn, t);
            snprintf(s_last, sizeof s_last, "%s%s", k.btn == BTN_UP ? "UP" : k.btn == BTN_DOWN ? "DOWN"
                     : k.btn == BTN_LEFT ? "LEFT" : k.btn == BTN_RIGHT ? "RIGHT" : "OK",
                     ok ? "" : "  (무시됨)");
            break;
        }
        case K_STOP:  logic_stop(m, t);                     snprintf(s_last, sizeof s_last, "원격 STOP");      break;
        case K_FAULT: logic_fault(m, FAULT_SENSE_STALL, t); snprintf(s_last, sizeof s_last, "센서 고장 흉내"); break;
        case K_POWER: logic_init(m, t);                     snprintf(s_last, sizeof s_last, "전원 재인가");    break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// 출력
// ---------------------------------------------------------------------------

// 대화형: 백라이트 LCD 처럼 파란 바탕에 흰 글자로 그린다.
static void draw_tty(const AppModel *m) {
    ScreenLines L;
    logic_render(m, L);
    const char *why = "";
    const uint8_t duty = logic_output(m, &why);

    printf("\x1b[H\x1b[2J");
    printf("\n  Air-Rhythm LCD 시뮬레이터  (app_logic.cpp 그대로)\n\n");
    printf("    ┌──────────────────┐\n");
    for (int r = 0; r < 2; r++) printf("    │ \x1b[1;97;44m%s\x1b[0m │\n", L[r]);
    printf("    └──────────────────┘\n\n");
    printf("    모터  out=%-3u  (%s)\n", (unsigned)duty, why);
    printf("    화면  %s   강도 %s   고장 %s\n", screen_name(m->screen), level_name(m->level), fault_name(m->fault));
    printf("    입력  %s\n\n", s_last);
    printf("  ↑↓←→ 버튼   Enter/Space = OK   s = 원격 STOP\n");
    printf("  f = 센서 고장 흉내   r = 전원 재인가   q = 종료\n");
    fflush(stdout);
}

// 파이프: 시리얼 모니터와 같은 모양으로 찍는다.
static void draw_plain(const AppModel *m, const char *key) {
    ScreenLines L;
    logic_render(m, L);
    const char *why = "";
    const uint8_t duty = logic_output(m, &why);
    printf("%-6s +----------------+\n", key);
    printf("       |%s|  out=%u (%s)%s\n", L[0], (unsigned)duty, why,
           strstr(s_last, "무시") ? "  무시됨" : "");
    printf("       |%s|\n", L[1]);
    printf("       +----------------+\n");
}

// ---------------------------------------------------------------------------
// 대화형 모드 — 터미널 raw 입력
// ---------------------------------------------------------------------------

static termios s_saved;
static bool    s_raw = false;

static void restore_tty() {
    if (s_raw) { tcsetattr(STDIN_FILENO, TCSANOW, &s_saved); s_raw = false; }
    printf("\x1b[?25h");       // 커서 다시 보이기
    fflush(stdout);
}
static void on_signal(int) { restore_tty(); _exit(0); }

static Key read_key() {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return { K_NONE, BTN_OK };
    if (c == 0x1b) {                                   // 방향키: ESC [ A/B/C/D
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1 || seq[0] != '[') return { K_NONE, BTN_OK };
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return { K_NONE, BTN_OK };
        switch (seq[1]) {
            case 'A': return { K_BTN, BTN_UP };
            case 'B': return { K_BTN, BTN_DOWN };
            case 'C': return { K_BTN, BTN_RIGHT };
            case 'D': return { K_BTN, BTN_LEFT };
        }
        return { K_NONE, BTN_OK };
    }
    switch (c) {
        case '\r': case '\n': case ' ': return { K_BTN, BTN_OK };
        case 's': case 'S':             return { K_STOP, BTN_OK };
        case 'f': case 'F':             return { K_FAULT, BTN_OK };
        case 'r': case 'R':             return { K_POWER, BTN_OK };
        case 'q': case 'Q': case 3:     return { K_QUIT, BTN_OK };   // 3 = Ctrl+C
    }
    return { K_NONE, BTN_OK };
}

static int run_interactive() {
    tcgetattr(STDIN_FILENO, &s_saved);
    termios raw = s_saved;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    s_raw = true;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    printf("\x1b[?25l");        // 커서 숨기기

    AppModel m;
    logic_init(&m, now_ms());
    snprintf(s_last, sizeof s_last, "(부팅)");
    const SenseUpdate healthy = {};     // 센서는 늘 정상인 것으로 둔다 — 고장은 f 키로 흉내
    ScreenLines shown = {};
    bool dirty = true;

    for (;;) {
        // 펌웨어처럼 20ms 틱. 그 사이에 들어온 키를 처리한다.
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeval tv = { 0, PERIOD_MS * 1000 };
        if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
            Key k = read_key();
            if (k.kind == K_QUIT) break;
            if (k.kind != K_NONE) { apply_key(&m, k, now_ms()); dirty = true; }
        }
        logic_tick(&m, &healthy, false, now_ms());

        ScreenLines L;
        logic_render(&m, L);
        if (dirty || memcmp(L, shown, sizeof L) != 0) {
            memcpy(shown, L, sizeof L);
            draw_tty(&m);
            dirty = false;
        }
    }
    restore_tty();
    printf("\n");
    return 0;
}

// ---------------------------------------------------------------------------
// 파이프 모드 — 키 목록을 읽어 한 키마다 화면을 찍는다
// ---------------------------------------------------------------------------

static int run_script() {
    AppModel m;
    uint32_t t = 0;
    logic_init(&m, t);
    draw_plain(&m, "(boot)");

    char tok[16];
    while (scanf("%15s", tok) == 1) {
        t += INPUT_LOCKOUT_MS + 50;      // 사람이 누르는 간격 — 잠금에 걸리지 않게
        Key k = { K_NONE, BTN_OK };
        if      (!strcmp(tok, "U"))  k = { K_BTN, BTN_UP };
        else if (!strcmp(tok, "D"))  k = { K_BTN, BTN_DOWN };
        else if (!strcmp(tok, "L"))  k = { K_BTN, BTN_LEFT };
        else if (!strcmp(tok, "R"))  k = { K_BTN, BTN_RIGHT };
        else if (!strcmp(tok, "OK")) k = { K_BTN, BTN_OK };
        else if (!strcmp(tok, "S"))  k = { K_STOP, BTN_OK };
        else if (!strcmp(tok, "F"))  k = { K_FAULT, BTN_OK };
        else if (!strcmp(tok, "P"))  k = { K_POWER, BTN_OK };
        else { fprintf(stderr, "모르는 키: %s\n", tok); continue; }
        s_last[0] = '\0';
        apply_key(&m, k, t);
        draw_plain(&m, tok);
    }
    return 0;
}

int main() {
    return isatty(STDIN_FILENO) ? run_interactive() : run_script();
}
