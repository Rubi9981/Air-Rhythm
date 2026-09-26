// LCD1602 + 5방향 스위치 시뮬레이터 — 보드 없이 PC 터미널에서 화면 흐름을 확인한다.
//
// 펌웨어와 같은 app_logic.cpp 와 호흡 검출기(task_sense.cpp 의 sense_step)를 그대로 컴파일한다.
// 화면 문구·입력 잠금·FAULT·호흡 모드 규칙이 실제 기기와 한 글자도 다르지 않다.
// 다른 것은 입력(키보드)과 센서(합성 호흡 신호 또는 녹음 CSV), 출력(터미널)뿐이다.
//
//   sh firmware/tools/lcd_sim/run.sh                 합성 호흡 신호 (분당 15회)
//   sh firmware/tools/lcd_sim/run.sh data/유병오.csv  녹음 재생 (끝나면 처음부터 반복)
//
// 키:  ↑ ↓ ← →  방향 버튼      Enter / Space  OK(가운데)
//      b  호흡 켜기/끄기(스트랩 떨어짐 흉내)   s  원격 STOP   f  센서 고장 흉내
//      r  전원 재인가                          q  종료
//
// 표준입력이 터미널이 아니면(파이프) 키 목록을 읽어 화면이 바뀔 때마다 찍는다 — 자동 확인용.
//   토큰: U D L R OK S F P B  Wn(n초 흘려보내기)
//   printf 'D OK D OK W15 W3 OK\n' | ./lcd_sim

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <vector>

#include <Arduino.h>
#include "app_logic.h"
#include "app_types.h"
#include "board_config.h"
#include "breath_slope.h"
#include "csv.h"
#include "task_sense.h"

// ---------------------------------------------------------------------------
// 센서 — 합성 호흡 또는 녹음 CSV 를 한 샘플(20ms)씩 내준다
// ---------------------------------------------------------------------------

static std::vector<Row> s_csv;       // 비어 있으면 합성 신호
static size_t   s_csv_i     = 0;
static bool     s_breathing = true;  // b 키로 토글 — 끄면 평탄한 신호(스트랩 떨어짐)
static uint32_t s_sample_n  = 0;
static uint32_t s_rng       = 12345;

static float noise_mv() {            // 작은 잡음 ±0.5mV — 결정적(같은 입력이면 같은 결과)
    s_rng = s_rng * 1103515245u + 12345u;
    return ((s_rng >> 16) & 0x3FF) / 1023.0f - 0.5f;
}

static int16_t next_mv() {
    s_sample_n++;
    if (!s_csv.empty()) {
        const int16_t mv = (int16_t)s_csv[s_csv_i].mv;
        s_csv_i = (s_csv_i + 1) % s_csv.size();
        return s_breathing ? mv : (int16_t)900;
    }
    const float t = s_sample_n * (PERIOD_MS / 1000.0f);
    const float breath = s_breathing ? 15.0f * sinf(2.0f * (float)M_PI * t / 4.0f) : 0.0f;  // 4초 주기 = 분당 15회
    return (int16_t)lroundf(900.0f + breath + noise_mv());
}

// 펌웨어 한 틱: 센서 태스크(초기화 요청 반영 + sense_step) → 앱 태스크(logic_tick, 초기화 요청 전달)
static SenseUpdate s_u = {};
static uint16_t    s_epoch = 0;

static void tick(AppModel *m, uint32_t now) {
    // task_app: 호흡 모드가 원하면 초기화를 요청한다. task_sense: 다음 틱 시작에 초기화한다.
    if (logic_reset_wanted(m)) {
        logic_reset_issued(m, ++s_epoch);
        sense_reset();
        s_u = {};
    }
    s_u.epoch = s_epoch;
    const int16_t mv = next_mv();
    sense_step(mv, mv, &s_u);
    logic_tick(m, &s_u, false, now);
}

static uint8_t output(const AppModel *m, const char **why) {
    return logic_output(m, &s_u, false, why);
}

static void clear_events() { s_u.events = 0; }

static uint32_t now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

// ---------------------------------------------------------------------------
// 입력 → 모델
// ---------------------------------------------------------------------------

typedef enum { K_NONE, K_BTN, K_STOP, K_FAULT, K_POWER, K_BREATH, K_QUIT } KeyKind;
struct Key { KeyKind kind; Button btn; };

static char s_last[64] = "";

static const char *btn_label(Button b) {
    return b == BTN_UP ? "UP" : b == BTN_DOWN ? "DOWN" : b == BTN_LEFT ? "LEFT" : b == BTN_RIGHT ? "RIGHT" : "OK";
}

static void apply_key(AppModel *m, Key k, uint32_t t) {
    switch (k.kind) {
        case K_BTN: {
            const bool ok = logic_button(m, k.btn, t);
            snprintf(s_last, sizeof s_last, "%s%s", btn_label(k.btn), ok ? "" : "  (무시됨)");
            break;
        }
        case K_STOP:   logic_stop(m, t);                     snprintf(s_last, sizeof s_last, "원격 STOP");      break;
        case K_FAULT:  logic_fault(m, FAULT_SENSE_STALL, t); snprintf(s_last, sizeof s_last, "센서 고장 흉내"); break;
        case K_POWER:  logic_init(m, t);                     snprintf(s_last, sizeof s_last, "전원 재인가");    break;
        case K_BREATH:
            s_breathing = !s_breathing;
            snprintf(s_last, sizeof s_last, "호흡 %s", s_breathing ? "켬" : "끔 (스트랩 떨어짐)");
            break;
        default: break;
    }
}

static const char *phase_label(int8_t ph) {
    return ph == BR_FALLING ? "호기" : ph == BR_RISING ? "흡기" : "모름";
}

// ---------------------------------------------------------------------------
// 출력
// ---------------------------------------------------------------------------

// 대화형: 백라이트 LCD 처럼 파란 바탕에 흰 글자로 그린다.
static void draw_tty(const AppModel *m) {
    ScreenLines L;
    logic_render(m, L);
    const char *why = "";
    const uint8_t duty = output(m, &why);

    printf("\x1b[H\x1b[2J");
    printf("\n  Air-Rhythm LCD 시뮬레이터  (app_logic.cpp + 호흡 검출기 그대로)\n\n");
    printf("    ┌──────────────────┐\n");
    for (int r = 0; r < 2; r++) printf("    │ \x1b[1;97;44m%s\x1b[0m │\n", L[r]);
    printf("    └──────────────────┘\n\n");
    printf("    모터  out=%-3u  (%s)\n", (unsigned)duty, why);
    printf("    화면  %s   강도 %s   고장 %s\n", screen_name(m->screen), level_name(m->level), fault_name(m->fault));
    printf("    센서  %s  %s  위상 %s  bpm %.1f  amp %.1fmV  %s %s\n",
           s_csv.empty() ? "합성" : "CSV", s_breathing ? "호흡 켬" : "호흡 끔",
           phase_label(s_u.phase), (double)s_u.bpm, (double)s_u.amp,
           (s_u.flags & FLAG_SETTLED) ? "정착" : "정착 중", (s_u.flags & FLAG_SIGNAL_OK) ? "신호 정상" : "무신호");
    printf("    입력  %s\n\n", s_last);
    printf("  ↑↓←→ 버튼   Enter/Space = OK   b = 호흡 켜기/끄기   s = 원격 STOP\n");
    printf("  f = 센서 고장 흉내   r = 전원 재인가   q = 종료\n");
    fflush(stdout);
}

// 파이프: 시리얼 모니터와 같은 모양으로 찍는다. at 은 경과 시간(초).
static void draw_plain(const AppModel *m, const char *key, double at) {
    ScreenLines L;
    logic_render(m, L);
    const char *why = "";
    const uint8_t duty = output(m, &why);
    printf("%-5s %5.1fs +----------------+\n", key, at);
    printf("             |%s|  out=%u (%s)%s\n", L[0], (unsigned)duty, why,
           strstr(s_last, "무시") ? "  무시됨" : "");
    printf("             |%s|\n", L[1]);
    printf("             +----------------+\n");
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
        case 'b': case 'B':             return { K_BREATH, BTN_OK };
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
    uint32_t next = now_ms();
    uint32_t last_draw = 0;
    bool dirty = true;

    for (;;) {
        // 펌웨어처럼 20ms 마다 한 샘플. 그 사이에 들어온 키를 처리한다.
        const uint32_t now = now_ms();
        const uint32_t wait_ms = (int32_t)(next - now) > 0 ? next - now : 0;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeval tv = { 0, (suseconds_t)(wait_ms * 1000) };
        if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
            Key k = read_key();
            if (k.kind == K_QUIT) break;
            if (k.kind != K_NONE) { apply_key(&m, k, now_ms()); dirty = true; }
            continue;
        }
        next += PERIOD_MS;
        tick(&m, now_ms());
        if (s_u.events) dirty = true;
        clear_events();

        // 화면이 바뀌면 바로, 아니어도 센서 줄 갱신을 위해 0.2초마다 다시 그린다
        if (dirty || now_ms() - last_draw >= 200) {
            draw_tty(&m);
            last_draw = now_ms();
            dirty = false;
        }
    }
    restore_tty();
    printf("\n");
    return 0;
}

// ---------------------------------------------------------------------------
// 파이프 모드 — 키 목록을 읽어 화면이 바뀔 때마다 찍는다
// ---------------------------------------------------------------------------

static int run_script() {
    AppModel m;
    uint32_t t = 0;
    logic_init(&m, t);
    ScreenLines shown;
    logic_render(&m, shown);
    draw_plain(&m, "boot", 0);

    auto advance = [&](uint32_t ms, const char *label) {
        for (uint32_t e = 0; e < ms; e += PERIOD_MS) {
            t += PERIOD_MS;
            tick(&m, t);
            clear_events();
            ScreenLines L;
            logic_render(&m, L);
            if (memcmp(L, shown, sizeof L) != 0) {
                memcpy(shown, L, sizeof L);
                s_last[0] = '\0';
                draw_plain(&m, label, t / 1000.0);
            }
        }
    };

    char tok[16];
    while (scanf("%15s", tok) == 1) {
        if (tok[0] == 'W') { advance((uint32_t)(atof(tok + 1) * 1000), tok); continue; }
        advance(INPUT_LOCKOUT_MS + 50, "");          // 사람이 누르는 간격 — 잠금에 걸리지 않게
        Key k = { K_NONE, BTN_OK };
        if      (!strcmp(tok, "U"))  k = { K_BTN, BTN_UP };
        else if (!strcmp(tok, "D"))  k = { K_BTN, BTN_DOWN };
        else if (!strcmp(tok, "L"))  k = { K_BTN, BTN_LEFT };
        else if (!strcmp(tok, "R"))  k = { K_BTN, BTN_RIGHT };
        else if (!strcmp(tok, "OK")) k = { K_BTN, BTN_OK };
        else if (!strcmp(tok, "S"))  k = { K_STOP, BTN_OK };
        else if (!strcmp(tok, "F"))  k = { K_FAULT, BTN_OK };
        else if (!strcmp(tok, "P"))  k = { K_POWER, BTN_OK };
        else if (!strcmp(tok, "B"))  k = { K_BREATH, BTN_OK };
        else { fprintf(stderr, "모르는 키: %s\n", tok); continue; }
        s_last[0] = '\0';
        apply_key(&m, k, t);
        logic_render(&m, shown);
        draw_plain(&m, tok, t / 1000.0);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        s_csv = read_csv(argv[1]);
        if (s_csv.empty()) { fprintf(stderr, "빈 CSV: %s\n", argv[1]); return 1; }
    }
    sense_reset();
    return isatty(STDIN_FILENO) ? run_interactive() : run_script();
}
