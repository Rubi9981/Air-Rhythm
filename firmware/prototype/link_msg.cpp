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
    char line[96];
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
 *   [6] Motor Active (0x00: OFF, 0x01: ON)
 *   [7] Power Status (0x64 = 100%, 유선 상시 전원)
 *   [8] Current Strike Period Low Byte (uint16 Little Endian, ms)
 *   [9] Current Strike Period High Byte
 *   [10] Error Code (0x00: Normal)
 *   [11] Checksum (XOR of Bytes 2..10)
 */
void msg_send_telemetry(const SenseUpdate *sense, uint8_t deviceState,
                        bool motorActive, uint16_t currentPeriodMs) {
    if (sense == nullptr) return;

    uint8_t pkt[12];

    // [0-1] 헤더
    pkt[0] = 0x55;
    pkt[1] = 0xAA;

    // [2] 기기 상태
    pkt[2] = deviceState;

    // [3-4] 흉부 센서 압력값 (밀리볼트 mV -> signed 16-bit LE)
    int16_t pressure_mv = sense->mv;
    pkt[3] = (uint8_t)(pressure_mv & 0xFF);
    pkt[4] = (uint8_t)((pressure_mv >> 8) & 0xFF);

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

    // [6] 모터 현재 활성 여부
    pkt[6] = motorActive ? 0x01 : 0x00;

    // [7] 전원 상태 (유선 공급으로 항상 100%)
    pkt[7] = 0x64;

    // [8-9] 현재 설정된 타격 주기
    pkt[8] = (uint8_t)(currentPeriodMs & 0xFF);
    pkt[9] = (uint8_t)((currentPeriodMs >> 8) & 0xFF);

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

    // 최초 필터 정착(Settle) 완료 알림 (1회만 발생)
    static bool s_announced_settled = false;
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

static MsgSink sink_of(CmdSource src) {
    return (src == SRC_BLE) ? SINK_BLE : SINK_SERIAL;
}

void msg_ack(CmdSource src, const Command *cmd) {
    if (cmd == nullptr) return;

    if (cmd->arg != 0) {
        msg_emitf(sink_of(src), "OK %s %ld", cmd_name(cmd->type), (long)cmd->arg);
    } else {
        msg_emitf(sink_of(src), "OK %s", cmd_name(cmd->type));
    }
}

void msg_ack_err(CmdSource src, const char *why) {
    msg_emitf(sink_of(src), "ERR %s", (why != nullptr) ? why : "unknown");
}

void msg_snapshot(const SenseUpdate *sense, bool motor_running, uint16_t period_ms) {
    if (sense == nullptr) return;

    // 기기 연결 직후 앱에 전체 상태를 브로드캐스트
    msg_emitf(SINK_BOTH,
              "STATE running=%d period=%u bpm=%.1f amp=%.1f settled=%d sig=%d",
              motor_running ? 1 : 0,
              (unsigned)period_ms,
              sense->bpm,
              sense->amp,
              (sense->flags & FLAG_SETTLED) ? 1 : 0,
              (sense->flags & FLAG_SIGNAL_OK) ? 1 : 0);
}
