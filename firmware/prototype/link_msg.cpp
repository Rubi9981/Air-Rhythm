/**
 * ============================================================================
 * [link_msg.cpp] 출력 계층 구현체 (12바이트 텔레메트리 + 시리얼 디버그 출력)
 * ============================================================================
 *
 * 역할:
 *   - 12바이트 바이너리 텔레메트리 패킷 빌드 및 BLE Notify 전송 (20Hz)
 *   - 호흡 이벤트(흡기/호기 Onset, 무신호 등) 시리얼 및 BLE 라인 출력
 *   - 명령 수신 응답(ACK / ERR) 및 기기 상태 스냅샷 전송
 * ============================================================================
 */

#include "link_msg.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

#include "board_config.h"
#include "link_ble.h"
#include "link_cmd.h"
#include "breath_slope.h"   // BR_RISING(+1), BR_FALLING(-1), BR_UNKNOWN(0)

// 센서 태스크 멈춤(Stall) 상태 추적용 플래그
static bool s_sense_stalled = false;

// 정착 완료는 회차마다 한 번씩 알린다. 검출기를 초기화하면 다시 알릴 수 있게 된다.
static uint16_t s_epoch             = 0;
static bool     s_announced_settled = false;

// ============================================================================
// [1. 기본 메시지 방출 (Emit) 헬퍼]
// ============================================================================

void msg_emit(MsgSink to, const char *line) {
    if (line == nullptr) return;

    if (to & SINK_SERIAL) {
        Serial.println(line);
    }
    if (to & SINK_BLE) {
        ble_send_line(line);  // BLE 미연결 시 내부에서 자동 폐기
    }
}

void msg_emitf(MsgSink to, const char *fmt, ...) {
    char line[128];                               // STATE 줄이 가장 길다(약 85자)
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    msg_emit(to, line);
}

// ============================================================================
// [2. 12바이트 바이너리 텔레메트리 패킷 빌더] - BLE 통신 전용 (20Hz)
// ============================================================================
/*
 * 패킷 구조 (12 Bytes):
 *   [0] 0x55 (Header 1)
 *   [1] 0xAA (Header 2)
 *   [2] Device State (0x00: IDLE, 0x01: RUNNING, 0x02: CALIBRATING, 0xFF: ERROR)
 *   [3] Chest Pressure Low Byte (int16 Little Endian, mV 단위)
 *   [4] Chest Pressure High Byte
 *   [5] Respiration Phase (0x00: NONE, 0x01: INHALE, 0x02: EXHALE)
 *   [6] Motor On (0x00: OFF, 0x01: ON) — 명령 상태. 실제로 도는지는 [9] 를 볼 것
 *   [7] Power Status (0x64 = 100%, 유선 상시 전원)
 *   [8] Duty (0~255) — 명령으로 설정된 값
 *   [9] Out  (0~255) — 호기 게이트를 거쳐 지금 실제로 나가는 값
 *   [10] Error Code (0x00: Normal)
 *   [11] Checksum (XOR of Bytes 2..10)
 *
 * [6]=1 인데 [9]=0 이면 "켜져 있지만 흡기 중(또는 정착 전·무신호)이라 대기" 다.
 * 이유까지 필요하면 STATE 텍스트 줄의 gate= 를 본다.
 */
void msg_send_telemetry(const SenseUpdate *sense, uint8_t deviceState,
                        bool motor_on, uint8_t duty, uint8_t out) {
    if (sense == nullptr) return;

    uint8_t pkt[12];

    // [0-1] 헤더
    pkt[0] = 0x55;
    pkt[1] = 0xAA;

    // [2] 기기 상태
    pkt[2] = deviceState;

    // [3-4] 흉부 센서 압력값 (필터링 후 호흡 파형 -> signed 16-bit LE)
    int16_t pressure_filt = sense->filt;
    pkt[3] = (uint8_t)(pressure_filt & 0xFF);
    pkt[4] = (uint8_t)((pressure_filt >> 8) & 0xFF);

    // [5] 호흡 위상 매핑
    //   BR_RISING(+1)  -> 0x01 (INHALE: 들숨)
    //   BR_FALLING(-1) -> 0x02 (EXHALE: 날숨)
    //   기타(0)        -> 0x00 (NONE: 판정 불가)
    uint8_t phase_code = 0x00;
    if (sense->phase == BR_RISING) {
        phase_code = 0x01;
    } else if (sense->phase == BR_FALLING) {
        phase_code = 0x02;
    }
    pkt[5] = phase_code;

    // [6] 모터 명령 상태
    pkt[6] = motor_on ? 0x01 : 0x00;

    // [7] 전원 상태 (유선 공급으로 항상 100%)
    pkt[7] = 0x64;

    // [8] 설정 duty, [9] 실제 출력 duty
    pkt[8] = duty;
    pkt[9] = out;

    // [10] 에러 코드
    pkt[10] = 0x00;

    // [11] 체크섬 계산: Byte[2] ~ Byte[10] XOR
    uint8_t checksum = 0;
    for (int i = 2; i <= 10; i++) {
        checksum ^= pkt[i];
    }
    pkt[11] = checksum;

    // BLE Notify 전송
    ble_send_telemetry(pkt, sizeof(pkt));
}

// ============================================================================
// [3. 시리얼 디버그 및 이벤트 리포트]
// ============================================================================

static void report_onset(const SenseUpdate *sense, const char *kind) {
    const unsigned long delay_ms = (unsigned long)(sense->ev_n - sense->ext_n) * PERIOD_MS;
    char line[96];

    int written = snprintf(line, sizeof(line), "# %s n=%lu delay=%lums",
                          kind, (unsigned long)sense->ev_n, delay_ms);

    if (sense->bpm > 0.0f && written > 0 && written < (int)sizeof(line)) {
        snprintf(line + written, sizeof(line) - written, " bpm=%.1f", sense->bpm);
    }

    msg_emit(SINK_BOTH, line);
}

static void report_signal(const SenseUpdate *sense, const char *kind) {
    msg_emitf(SINK_BOTH, "# %s n=%lu amp=%.1f", kind, (unsigned long)sense->ev_n, sense->ev_amp);
}

void msg_report(const SenseUpdate *sense) {
    if (sense == nullptr) return;

    // 센서 태스크 복구 감지
    if (s_sense_stalled) {
        s_sense_stalled = false;
        msg_emit(SINK_BOTH, "# FAULT sense_ok");
    }

    // 검출기 초기화 후 첫 샘플. 이 줄 이후의 이벤트는 새 회차의 것이다.
    if (sense->epoch != s_epoch) {
        s_epoch = sense->epoch;
        s_announced_settled = false;
        msg_emitf(SINK_BOTH, "# RESET epoch=%u", (unsigned)s_epoch);
    }

    // 큐 드롭(과부하) 감지
    if (sense->drops > 0) {
        msg_emitf(SINK_BOTH, "# QDROP n=%lu ticks=%u", (unsigned long)sense->n, sense->drops);
    }

    // 1초 단위 샘플링 주기 진단 출력
    if (REPORT_RATE && sense->rate_ready) {
        msg_emitf(SINK_SERIAL, "# fs=%.2fHz avg=%luus min=%luus max=%luus",
                  sense->rate_fs,
                  (unsigned long)sense->rate_avg_us,
                  (unsigned long)sense->rate_min_us,
                  (unsigned long)sense->rate_max_us);
    }

    // 샘플 단위 출력 (raw<TAB>mv)
    if (REPORT_SAMPLE) {
        msg_emitf(SINK_SERIAL, "%d\t%d", sense->raw, sense->mv);
    }

    // 호흡 검출 이벤트 출력
    if (sense->events & EVENT_INHALE)      report_onset(sense,  "INHALE");
    if (sense->events & EVENT_EXHALE)      report_onset(sense,  "EXHALE");
    if (sense->events & EVENT_SIGNAL_LOST) report_signal(sense, "NOSIG");
    if (sense->events & EVENT_SIGNAL_OK)   report_signal(sense, "SIGOK");

    // 필터 정착(Settle) 완료 알림 (회차마다 1회)
    if (!s_announced_settled && (sense->flags & FLAG_SETTLED)) {
        s_announced_settled = true;
        msg_emit(SINK_BOTH, "# SETTLED");
    }
}

void msg_sense_stall() {
    if (!s_sense_stalled) {
        s_sense_stalled = true;
        msg_emit(SINK_BOTH, "# FAULT sense_stall");
    }
}

// ============================================================================
// [4. 명령 응답 및 상태 스냅샷]
// ============================================================================

// ACK 는 명령이 온 곳으로만 보낸다. 시리얼로 친 명령의 답이 앱 화면에 뜨면 혼란스럽다.
static MsgSink sink_of(CmdSource src) {
    return (src == SRC_BLE) ? SINK_BLE : SINK_SERIAL;
}

void msg_ack(CmdSource src, const Command *cmd) {
    if (cmd == nullptr) return;

    // 인자를 붙일지는 명령의 종류로 정한다. 값이 0 인지로 판단하면
    // "DUTY 0" 의 답이 "OK DUTY" 가 되어 버린다 — 0 도 유효한 값이다.
    if (cmd->type == CMD_SET_DUTY) {
        msg_emitf(sink_of(src), "OK %s %ld", cmd_name(cmd->type), (long)cmd->arg);
    } else {
        msg_emitf(sink_of(src), "OK %s", cmd_name(cmd->type));
    }
}

void msg_ack_err(CmdSource src, const char *why) {
    msg_emitf(sink_of(src), "ERR %s", (why != nullptr) ? why : "unknown");
}

void msg_snapshot(const SenseUpdate *sense, bool motor_on, uint8_t duty,
                  uint8_t out, const char *gate) {
    if (sense == nullptr) return;

    // 한 줄로 현재 상태 전부. 앱이 재연결했을 때 이 줄만으로 화면을 다시 그릴 수 있어야 한다.
    //
    // duty 와 out 을 나눠 싣는 이유: 호기 게이트가 닫혀 있으면 둘이 다르다.
    // "명령은 들어갔는데 왜 안 도나" 를 gate 한 단어로 답하게 하려는 것이다.
    msg_emitf(SINK_BOTH,
              "STATE motor=%s duty=%u out=%u gate=%s bpm=%.1f amp=%.1f settled=%d sig=%d",
              motor_on ? "on" : "off",
              (unsigned)duty,
              (unsigned)out,
              gate ? gate : "?",
              sense->bpm,
              sense->amp,
              (sense->flags & FLAG_SETTLED) ? 1 : 0,
              (sense->flags & FLAG_SIGNAL_OK) ? 1 : 0);
}
