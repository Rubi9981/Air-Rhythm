// 호흡 모드 재생 검증 — 녹음 CSV 를 실제 검출기(sense_step)와 상태 머신(app_logic)에 함께 흘려,
// 사람이 호흡 모드를 시작한 것처럼 끝까지 돌린다.
//
// NO_SIGNAL / SIGNAL_LOST 가 되면 사람이 1초 뒤 RETRY 를 누른 것으로 치고 계속 재생한다.
//
// 샘플마다 아래 안전 규칙을 확인한다. 하나라도 어기면 실패다.
//   1. 모터가 도는 것은 BREATH_RUN 이고, 그 샘플이 호기·정착·신호 정상일 때뿐이다.
//   2. (재)초기화 후 첫 호기 "시작 이벤트" 를 보기 전에는 절대 돌지 않는다.
//   3. 실행 중 신호를 잃으면 그 샘플에서 SIGNAL_LOST 로 간다. 그 뒤 RETRY 를 누르기 전에는
//      실행으로 돌아가지 않는다.
//   4. 호흡이 전혀 없는 긴 녹음은 NO_SIGNAL 로 끝난다.
// 결과(실행까지 걸린 시간, RETRY 횟수, 타진 비율)는 참고용으로 찍는다.
//
//   drv: breath_replay <csv>...     (logic.sh 가 data/*.csv 로 부른다)

#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include "app_logic.h"
#include "app_types.h"
#include "board_config.h"
#include "breath_slope.h"
#include "csv.h"
#include "task_sense.h"

static int g_fail = 0;

static void fail(const char *file, unsigned long n, const char *why) {
    g_fail++;
    printf("    FAIL %s  샘플 %lu: %s\n", file, n, why);
}

// 한 파일을 재생한다.
static void replay(const char *path) {
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;

    AppModel m;
    uint32_t t = 0;
    logic_init(&m, t);
    // 모드 선택 → 호흡 → START. 사람 손 속도로 누른다.
    const Button keys[] = { BTN_DOWN, BTN_OK, BTN_DOWN, BTN_OK };
    for (Button b : keys) { t += 300; logic_button(&m, b, t); }
    if (m.screen != SCR_BREATH_INIT) { fail(name, 0, "START 로 BREATH_INIT 에 가지 못함"); return; }

    SenseUpdate u = {};
    uint16_t epoch = 0;
    unsigned long n = 0, run_samples = 0, hit_samples = 0, breaths = 0, retries = 0;
    unsigned long run_at = 0;
    bool seen_exhale_after_settle = false;
    bool stopped = false;                // SIGNAL_LOST / NO_SIGNAL 에 들어와 RETRY 를 기다리는 중
    bool saw_no_signal = false;
    uint32_t stopped_at = 0;

    for (auto &r : read_csv(path)) {
        // task_app 이 하는 일: 초기화 요청 → 센서 태스크가 틱 경계에서 초기화 → 이후 샘플은 새 회차
        if (logic_reset_wanted(&m)) {
            logic_reset_issued(&m, ++epoch);
            sense_reset();
            u = {};
            seen_exhale_after_settle = false;
        }
        u.epoch = epoch;
        sense_step((int16_t)r.raw, (int16_t)r.mv, &u);
        t += PERIOD_MS;
        n++;
        if (u.events & (EVENT_INHALE | EVENT_EXHALE)) breaths++;

        const Screen before = m.screen;
        logic_tick(&m, &u, false, t);
        const uint8_t duty = logic_output(&m, &u, false, nullptr);

        if (before == SCR_BREATH_WAIT && (u.events & EVENT_EXHALE)) seen_exhale_after_settle = true;
        const bool signal_bad = (u.events & EVENT_SIGNAL_LOST) || !(u.flags & FLAG_SIGNAL_OK);
        if (before == SCR_BREATH_RUN && signal_bad && m.screen != SCR_SIGNAL_LOST)
            fail(name, n, "실행 중 신호 유실인데 SIGNAL_LOST 로 가지 않음");
        if (m.screen == SCR_BREATH_RUN) {
            run_samples++;
            if (!run_at) run_at = n;
            if (stopped) fail(name, n, "RETRY 없이 실행으로 돌아감");
        }
        if (m.screen == SCR_NO_SIGNAL) saw_no_signal = true;
        if (!stopped && (m.screen == SCR_SIGNAL_LOST || m.screen == SCR_NO_SIGNAL)) {
            stopped = true;
            stopped_at = t;
        }

        if (duty) {
            hit_samples++;
            if (m.screen != SCR_BREATH_RUN)       fail(name, n, "실행 화면이 아닌데 출력");
            if (u.phase != BR_FALLING)            fail(name, n, "호기가 아닌데 출력");
            if (!(u.flags & FLAG_SETTLED))        fail(name, n, "정착 전인데 출력");
            if (!(u.flags & FLAG_SIGNAL_OK))      fail(name, n, "무신호인데 출력");
            if (!seen_exhale_after_settle)        fail(name, n, "호기 시작 이벤트 전에 출력");
            if (stopped)                          fail(name, n, "신호 유실 뒤 RETRY 없이 출력");
            if (duty != DUTY_LOW)                 fail(name, n, "강도와 다른 duty");
        }
        if (g_fail > 20) return;             // 같은 실수를 수천 줄 찍지 않게
        u.events = 0;

        // 사람이 화면을 보고 1초 뒤 RETRY(커서 기본값)를 누른다
        if (stopped && t - stopped_at >= 1000) {
            if (logic_button(&m, BTN_OK, t)) retries++;
            stopped = false;
        }
    }

    // 호흡이 한 번도 잡히지 않은 녹음이 정착 + 대기 한도보다 길면 NO_SIGNAL 이어야 한다.
    // (0.3초짜리처럼 짧은 녹음은 판단할 수 없으므로 뺀다)
    const unsigned long judge_ms = (unsigned long)(SETTLE_S * 1000) + BREATH_WAIT_TIMEOUT_MS + 1000;
    if (breaths == 0 && n * PERIOD_MS > judge_ms && (run_at || !saw_no_signal))
        fail(name, n, "호흡이 없는 녹음인데 NO_SIGNAL 로 가지 않음");

    printf("  %-28s %6.1fs  ", name, n * PERIOD_MS / 1000.0);
    if (run_at) printf("실행 %5.1fs부터  RETRY %lu회  실행 중 타진 %2lu%%",
                       run_at * PERIOD_MS / 1000.0, retries,
                       run_samples ? hit_samples * 100 / run_samples : 0);
    else        printf("실행 못 함 (끝: %s, RETRY %lu회)", screen_name(m.screen), retries);
    printf("\n");
}

int main(int argc, char **argv) {
    puts("호흡 모드 재생 (실제 검출기 + 상태 머신)");
    for (int i = 1; i < argc; i++) replay(argv[i]);
    printf(g_fail ? "→ 안전 규칙 위반 %d건\n" : "→ 모든 녹음에서 안전 규칙 지킴\n", g_fail);
    return g_fail ? 1 : 0;
}
