/**
 * ============================================================================
 * [task_app.cpp] Application Core 태스크 구현체 (Core 0 실행)
 * ============================================================================
 *
 * 역할:
 *   - 센서 태스크(Core 1)로부터 큐(q_sense)를 통해 전달된 SenseUpdate 소비 (50Hz)
 *   - BLE/시리얼 명령 큐(q_cmd)로부터 명령을 수신하여 기기 상태 머신 업데이트
 *   - 20Hz 주기로 BLE 텔레메트리 패킷 송신 (msg_send_telemetry)
 *   - BLE 연결 끊김 시 Fail-Safe (타격 정지 및 IDLE 복귀)
 *   - 시리얼 디버그 리포트 방출 (msg_report)
 * ============================================================================
 */

#include "task_app.h"

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_types.h"
#include "board_config.h"
#include "link_ble.h"
#include "link_cmd.h"
#include "link_msg.h"

// ============================================================================
// [기기 내부 상태 구조체] - app_task만 소유하며 직접 수정
// ============================================================================
typedef struct {
    bool     motor_running;        // 모터 주기 타격 활성화 여부
    uint16_t strike_period_ms;     // 타격 주기 (200 ~ 2000 ms, 기본 500ms)
    uint8_t  device_state;         // 0x00: IDLE, 0x01: RUNNING, 0x02: CALIBRATING, 0xFF: ERROR
} AppState;

static AppState    s_app_state    = { false, 500, 0x00 };
static SenseUpdate s_latest_sense = {};   // 재연결 스냅샷 응답용 최근 센서 상태

// 텔레메트리 패킷 송신 주기 (ms) -> 50ms = 20Hz
static const unsigned long TELEMETRY_INTERVAL_MS = 50;

// ============================================================================
// [명령 처리 함수] - q_cmd로부터 수신된 명령을 상태에 반영
// ============================================================================
static void apply_command(const Command *cmd) {
    if (cmd == nullptr) return;

    switch (cmd->type) {
        case CMD_BLE_CONNECTED:
            // BLE 연결 수립 시 현재 기기 상태 스냅샷을 즉시 회신
            msg_snapshot(&s_latest_sense, s_app_state.motor_running, s_app_state.strike_period_ms);
            break;

        case CMD_BLE_DISCONNECTED:
            // ★ [Fail-Safe] BLE 연결 끊김 발생 시 즉시 타격 정지 (안전 조치)
            s_app_state.motor_running = false;
            s_app_state.device_state  = 0x00;  // STATE_IDLE
            // TODO(보드 수령 후): digitalWrite(PIN_MOTOR, LOW);
            break;

        case CMD_START:
            // 타격 시작: 주기(ms) 검증 후 RUNNING 상태 전환
            if (cmd->arg >= 200 && cmd->arg <= 2000) {
                s_app_state.strike_period_ms = (uint16_t)cmd->arg;
            } else {
                s_app_state.strike_period_ms = 500;
            }
            s_app_state.motor_running = true;
            s_app_state.device_state  = 0x01;  // STATE_RUNNING
            // TODO(보드 수령 후): 모터 타이머/펄스 구동 활성화
            msg_ack(cmd->src, cmd);
            break;

        case CMD_STOP:
            // 정상 정지: 모터 정지 및 IDLE 상태 전환
            s_app_state.motor_running = false;
            s_app_state.device_state  = 0x00;  // STATE_IDLE
            // TODO(보드 수령 후): digitalWrite(PIN_MOTOR, LOW);
            msg_ack(cmd->src, cmd);
            break;

        case CMD_EMERGENCY_STOP:
            // ★ [긴급 정지] 즉시 모든 출력 차단
            s_app_state.motor_running = false;
            s_app_state.device_state  = 0x00;  // STATE_IDLE
            // TODO(보드 수령 후): digitalWrite(PIN_MOTOR, LOW);
            msg_ack(cmd->src, cmd);
            break;

        case CMD_SET_PERIOD:
            // 동작 중 타격 주기 실시간 변경
            if (cmd->arg >= 200 && cmd->arg <= 2000) {
                s_app_state.strike_period_ms = (uint16_t)cmd->arg;
            }
            msg_ack(cmd->src, cmd);
            break;

        case CMD_CALIBRATE:
            // 캘리브레이션 모드 진입
            s_app_state.motor_running = false;
            s_app_state.device_state  = 0x02;  // STATE_CALIBRATING
            // TODO(보드 수령 후): 센서 기준값 캘리브레이션 루틴 수행
            msg_ack(cmd->src, cmd);
            break;

        case CMD_STATUS:
            // 현재 상태 요청에 대한 스냅샷 전송
            msg_snapshot(&s_latest_sense, s_app_state.motor_running, s_app_state.strike_period_ms);
            break;

        default:
            msg_ack_err(cmd->src, "unknown_command");
            break;
    }
}

// 큐에 쌓인 명령들을 모두 소비하여 순차 적용
static void drain_commands() {
    Command cmd;
    while (xQueueReceive(q_cmd, &cmd, 0) == pdTRUE) {
        apply_command(&cmd);
    }
}

// ============================================================================
// [Core 0 메인 태스크 루프]
// ============================================================================
static void app_task(void *) {
    SenseUpdate sense;
    unsigned long last_telemetry_time = 0;

    for (;;) {
        // 1. 센서 태스크(Core 1)가 보낸 데이터 수신 대기 (데드맨 타임아웃 검사)
        if (xQueueReceive(q_sense, &sense, pdMS_TO_TICKS(SENSE_STALL_MS)) != pdTRUE) {
            msg_sense_stall();
            continue;
        }
        s_latest_sense = sense;

        // 2. 시리얼 입력 폴링 및 명령 큐 드레인
        cmd_poll_serial();
        drain_commands();

        // 3. BLE 유지보수 작업 (재광고 등 Non-blocking 처리)
        ble_tick();

        // 4. BLE 텔레메트리 주기적 송신 (20Hz)
        unsigned long now = millis();
        if (ble_is_connected() && (now - last_telemetry_time >= TELEMETRY_INTERVAL_MS)) {
            msg_send_telemetry(&sense,
                               s_app_state.device_state,
                               s_app_state.motor_running,
                               s_app_state.strike_period_ms);
            last_telemetry_time = now;
        }

        // 5. 시리얼 디버그 메시지 방출 (호흡 검출 이벤트 등)
        msg_report(&sense);
    }
}

void app_start() {
    xTaskCreatePinnedToCore(app_task,
                            "app_task",
                            APP_STACK,
                            nullptr,
                            APP_PRIORITY,
                            nullptr,
                            APP_CORE);
}
