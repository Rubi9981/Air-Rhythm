#include "link_msg.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

#include "board_config.h"
#include "link_ble.h"
#include "link_cmd.h"

static bool sense_stalled = false;   // 센서 태스크 정지 상태. 진입·복귀에서 한 번씩만 알린다

// ---------------------------------------------------------------------------
// 분배 — 문자열이 밖으로 나가는 유일한 통로
// ---------------------------------------------------------------------------

void msg_emit(MsgSink to, const char *line) {
    if (to & SINK_SERIAL) Serial.println(line);
    if (to & SINK_BLE)    ble_send_line(line);   // 미연결이면 안에서 버린다
}

void msg_emitf(MsgSink to, const char *fmt, ...) {
    char line[96];                                // 규약상 가장 긴 줄이 약 45자
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    msg_emit(to, line);
}

// ---------------------------------------------------------------------------
// 정기 보고
// ---------------------------------------------------------------------------

// 지연은 극점 → 확정까지 걸린 샘플 수. 오프라인 지표와 같은 값이다.
// ev_n 은 det.n 이 아니라 이벤트가 확정된 샘플이어야 한다(app_types.h 참조).
static void report_onset(const SenseUpdate *sense, const char *kind) {
    const unsigned long delay_ms = (unsigned long)(sense->ev_n - sense->ext_n) * PERIOD_MS;
    char line[96];
    int written = snprintf(line, sizeof line, "# %s n=%lu delay=%lums",
                     kind, (unsigned long)sense->ev_n, delay_ms);
    if (sense->bpm > 0.0f && written > 0 && written < (int)sizeof line)
        snprintf(line + written, sizeof line - written, " bpm=%.1f", sense->bpm);
    // TODO(구현): 유실 감지를 위해 seq 번호를 덧붙일지 결정.
    //   번들 API 의 notify() 는 반환값이 없어 전송 성공을 알 수 없다. 앱이 빠진
    //   번호를 보고 감지하게 하면 펌웨어가 재전송 버퍼를 들 필요가 없어진다.
    //   붙인다면 Serial 쪽 형식도 함께 바뀌므로 host_test 기준을 갱신할 것.
    msg_emit(SINK_BOTH, line);
}

static void report_signal(const SenseUpdate *sense, const char *kind) {
    msg_emitf(SINK_BOTH, "# %s n=%lu amp=%.1f", kind, (unsigned long)sense->ev_n, sense->ev_amp);
}

void msg_report(const SenseUpdate *sense) {
    if (sense_stalled) {                           // 샘플이 다시 오기 시작했다
        sense_stalled = false;
        msg_emit(SINK_BOTH, "# FAULT sense_ok");
    }

    // 큐가 차서 버린 틱이 있었다. 정상 동작에서는 절대 나오지 않는 줄이다.
    // 이 경우 아래 이벤트들의 n/amp 는 마지막 것만 남아 있다.
    if (sense->drops)
        msg_emitf(SINK_BOTH, "# QDROP n=%lu ticks=%u", (unsigned long)sense->n, sense->drops);

    if (REPORT_RATE && sense->rate_ready)
        msg_emitf(SINK_SERIAL, "# fs=%.2fHz avg=%luus min=%luus max=%luus",
                  sense->rate_fs, (unsigned long)sense->rate_avg_us,
                  (unsigned long)sense->rate_min_us, (unsigned long)sense->rate_max_us);

    if (REPORT_SAMPLE)                       // ★ 50Hz — 절대 SINK_BLE 로 보내지 않는다
        msg_emitf(SINK_SERIAL, "%d\t%d", sense->raw, sense->mv);

    if (sense->events & EVENT_INHALE)      report_onset(sense,  "INHALE");
    if (sense->events & EVENT_EXHALE)      report_onset(sense,  "EXHALE");
    if (sense->events & EVENT_SIGNAL_LOST) report_signal(sense, "NOSIG");
    if (sense->events & EVENT_SIGNAL_OK)   report_signal(sense, "SIGOK");

    // 정착 완료는 한 번만 알린다.
    static bool announced_settled = false;
    if (!announced_settled && (sense->flags & FLAG_SETTLED)) {
        announced_settled = true;
        msg_emit(SINK_BOTH, "# SETTLED");
    }

    // TODO(구현): 앱용 1Hz 상태 요약.
    //   rate_ready 가 정확히 1초마다 서므로 그대로 틱 소스로 쓴다.
    //   상태 요약은 "덮어쓰기형" 이라 전송에 실패해도 버린다 — 1초 뒤 더 최신 것이 온다.
    //   if (sense->rate_ready) msg_emitf(SINK_BLE, "STATE phase=%d bpm=%.1f amp=%.1f", ...);
}

void msg_sense_stall() {
    if (!sense_stalled) {
        sense_stalled = true;
        msg_emit(SINK_BOTH, "# FAULT sense_stall");
    }
}

// ---------------------------------------------------------------------------
// 명령 응답
// ---------------------------------------------------------------------------

// ACK 는 명령이 온 곳으로만 보낸다. 시리얼로 친 명령의 답이 앱 화면에 뜨면 혼란스럽다.
static MsgSink sink_of(CmdSource src) { return src == SRC_BLE ? SINK_BLE : SINK_SERIAL; }

void msg_ack(CmdSource src, const Command *cmd) {
    // TODO(구현): "OK MOTOR ON" / "OK DUTY 200" 형태.
    //   cmd_name(cmd->type) 를 쓰고, 인자가 있는 명령은 값도 붙일 것.
    (void)src; (void)cmd;
}

void msg_ack_err(CmdSource src, const char *why) {
    msg_emitf(sink_of(src), "ERR %s", why);
}

void msg_snapshot(const SenseUpdate *sense, bool motor_on, uint8_t duty) {
    // TODO(구현): 연결 직후와 CMD_STATUS 에 답하는 한 줄.
    //   예) "STATE motor=on duty=200 bpm=15.2 amp=24.4 settled=1 sig=1"
    //   앱이 재연결했을 때 이 한 줄로 현재 화면을 다시 그릴 수 있어야 한다.
    (void)sense; (void)motor_on; (void)duty;
}
