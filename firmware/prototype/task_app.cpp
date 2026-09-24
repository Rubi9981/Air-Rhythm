/**
 * ============================================================================
 * [task_app.cpp] Application Core 태스크 구현체 (Core 0 실행)
 * ============================================================================
 *
 * 역할:
 *   - 센서 태스크(Core 1)로부터 큐(q_sense)를 통해 전달된 SenseUpdate 소비 (50Hz)
 *   - 시리얼·버튼 명령 큐(q_cmd)를 화면 상태 머신(app_logic)에 반영
 *   - 상태 머신이 정한 duty 를 매 틱 motor_set() (레벨 트리거)
 *   - 화면이 바뀌면 LCD 두 줄을 알림 (지금은 시리얼에 LCD 모양으로, 5단계에서 실제 LCD)
 *   - 호흡 모드가 요청하면 검출기 초기화를 센서 태스크에 전달 (sense_request_reset)
 *   - 20Hz 주기로 BLE 텔레메트리 패킷 송신 (msg_send_telemetry)
 *   - 시리얼 디버그 리포트 방출 (msg_report)
 *
 * 판단은 전부 app_logic.cpp 에 있다. 이 파일은 큐·모터·메시지를 잇는 배선만 한다.
 * ============================================================================
 */

#include "task_app.h"

#include <Arduino.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "act_motor.h"
#include "app_logic.h"
#include "app_types.h"
#include "board_config.h"
#include "link_ble.h"
#include "link_cmd.h"
#include "link_msg.h"
#include "task_sense.h"

// 텔레메트리 [2] 기기 상태 코드. 0x02 는 예약(구 CALIBRATING).
enum : uint8_t {
    DEV_IDLE    = 0x00,
    DEV_RUNNING = 0x01,
    DEV_ERROR   = 0xFF,
};

// ============================================================================
// app_task 만 소유한다. 다른 태스크·콜백은 절대 직접 만지지 않는다.
// 바꾸려면 q_cmd 로 Command 를 보내야 한다.
// ============================================================================
static AppModel    s_model;
static SenseUpdate s_latest_sense = {};   // 스냅샷 응답용 최근 센서 상태
static ScreenLines s_shown        = {};   // 마지막으로 알린 화면 — 바뀐 때만 다시 알린다

// 텔레메트리 패킷 송신 주기 (ms) -> 50ms = 20Hz
static const unsigned long TELEMETRY_INTERVAL_MS = 50;

static uint8_t model_duty() {
    return s_model.screen == SCR_SERVICE ? s_model.service_duty : level_duty(s_model.level);
}

static uint8_t device_state() {
    if (s_model.fault != FAULT_NONE) return DEV_ERROR;
    return logic_is_running(&s_model) ? DEV_RUNNING : DEV_IDLE;
}

static void send_snapshot() {
    const char *gate = "?";
    logic_output(&s_model, &s_latest_sense, false, &gate);
    const StatusView v = {
        screen_name(s_model.screen),
        level_name(s_model.level),
        model_duty(),
        motor_get(),            // "지금 실제로 나가는 값" 은 판단값이 아니라 출력 계층에 묻는다
        gate,
        fault_name(s_model.fault),
    };
    msg_snapshot(&s_latest_sense, &v);
}

static void publish_screen() {
    ScreenLines now;
    logic_render(&s_model, now);
    if (memcmp(now, s_shown, sizeof now) != 0) {
        memcpy(s_shown, now, sizeof now);
        msg_screen(now[0], now[1]);
    }
}

// ============================================================================
// [명령 처리] - q_cmd 에서 꺼낸 명령을 상태 머신에 반영한다
//
// 여기서는 모델만 바꾼다. 모터는 루프의 motor_set(logic_output()) 가 매 틱
// 다시 판단하므로, 정지도 같은 틱(최대 20ms)에 출력에 반영된다.
// 명령은 들어온 순서대로 처리한다. 정지가 시작보다 앞서는 것은 app_logic 의
// 입력 잠금 예외(실행 화면의 OK)로 보장한다.
// ============================================================================
static void apply_command(const Command *cmd, uint32_t now_ms) {
    if (cmd == nullptr) return;

    switch (cmd->type) {
        case CMD_BLE_CONNECTED:
            send_snapshot();    // 모니터 앱이 현재 상태를 즉시 그린다
            break;

        case CMD_BUTTON:
            if (logic_button(&s_model, (Button)cmd->arg, now_ms)) msg_ack(cmd->src, cmd);
            else                                                  msg_ack_err(cmd->src, "ignored");
            break;

        case CMD_STOP:
            logic_stop(&s_model, now_ms);
            msg_ack(cmd->src, cmd);
            break;

        case CMD_SET_DUTY:
#if SERVICE_MODE
            if (logic_service(&s_model, (uint8_t)cmd->arg, now_ms)) msg_ack(cmd->src, cmd);
            else                                                    msg_ack_err(cmd->src, "busy");
#else
            msg_ack_err(cmd->src, "service_off");   // 평소 빌드에서는 화면 흐름 밖에서 모터를 켤 수 없다
#endif
            break;

        case CMD_STATUS:
            send_snapshot();
            break;

        // 검출기를 처음부터 다시 정착시킨다(디버그용). 호흡 모드 흐름 안에서는 그 흐름이
        // 검출기 회차를 관리하므로 끼어들지 않는다 — 초기화는 RETRY 로 한다.
        case CMD_BREATH_RESET:
            if (logic_in_breath_flow(&s_model)) {
                msg_ack_err(cmd->src, "busy");
            } else {
                sense_request_reset();
                msg_ack(cmd->src, cmd);
            }
            break;

        // 배선 검증용. 창(窓) 동안 이 루프가 멈추므로 모터가 서 있을 때만, 24V 를 넣기 전에만 쓴다.
        case CMD_SELFTEST:
            if (logic_is_running(&s_model) || motor_get() != 0) msg_ack_err(cmd->src, "busy");
            else                                                motor_selftest();
            break;

        default:
            msg_ack_err(cmd->src, "unknown");
            break;
    }
}

// 명령은 드물게 오므로 한 틱에 남은 것을 전부 비운다.
static void drain_commands(uint32_t now_ms) {
    Command cmd;
    while (xQueueReceive(q_cmd, &cmd, 0) == pdTRUE) {
        apply_command(&cmd, now_ms);
    }
}

// ============================================================================
// [Core 0 메인 태스크 루프]
// ============================================================================
static void app_task(void *) {
    SenseUpdate sense;
    unsigned long last_telemetry_time = 0;

    logic_init(&s_model, millis());   // 부팅은 항상 모드 선택 + LOW + 출력 0 — 이전 동작을 잇지 않는다
    publish_screen();

    for (;;) {
        // 1. 타임아웃이 곧 데드맨이다. 센서 태스크가 멈추면 여기서 잡힌다.
        //    정상이면 20ms 마다 오므로 SENSE_STALL_MS 까지 갈 일이 없다.
        //    주의: 큐가 "가득 찬" 상황은 여기서 안 걸린다 — 데이터가 있으므로 즉시 반환한다.
        //          그건 drops(# QDROP) 로 드러나고 logic_tick 이 FAULT 로 보낸다.
        const bool got = xQueueReceive(q_sense, &sense, pdMS_TO_TICKS(SENSE_STALL_MS)) == pdTRUE;
        const uint32_t now = millis();

        if (!got) {
            msg_sense_stall();
            logic_fault(&s_model, FAULT_SENSE_STALL, now);   // 전원을 다시 켜야 풀린다
            motor_set(0);
            // 센서가 죽어도 STATUS 는 답할 수 있어야 한다. 버튼은 FAULT 에서 모두 무시된다.
            cmd_poll_serial();
            drain_commands(now);
            ble_tick();
            publish_screen();
            continue;
        }
        s_latest_sense = sense;

        // 2. 고장 검사와 호흡 모드 전환을 명령보다 먼저 한다 — 같은 틱에 들어온 시작 버튼이
        //    고장을 앞지르지 않게. 밀린 샘플도 하나씩 전부 여기를 지나므로 이벤트를 놓치지 않는다.
        const bool backlog = uxQueueMessagesWaiting(q_sense) > 0;
        logic_tick(&s_model, &sense, backlog, now);

        // 3. 시리얼·버튼 명령을 여기서만 반영한다
        cmd_poll_serial();
        drain_commands(now);
        ble_tick();                // 콜백이 미뤄둔 일(재광고 등)

        // 호흡 모드가 시작(또는 RETRY)되었으면 검출기 초기화를 요청한다. 센서 태스크가
        // 다음 틱에 core 1 에서 초기화하고, 그 뒤 샘플에는 이 회차 번호가 붙어 온다.
        if (logic_reset_wanted(&s_model)) logic_reset_issued(&s_model, sense_request_reset());

        // 4. 액추에이터. 매 틱 재선언한다 — 이 호출이 끊기면 모터가 저절로 멈추므로
        //    루프 구조 자체가 워치독이 된다(act_motor.h 참조).
        //    호흡 실행 중에는 지금 든 샘플이 최신일 때(backlog 없음)만 위상을 믿는다.
        motor_set(logic_output(&s_model, &sense, backlog, nullptr));

        publish_screen();

        // 5. BLE 텔레메트리 주기적 송신 (20Hz). motor_set() 뒤에 두어 out 이 이번 틱 값이 되게 한다.
        unsigned long tnow = millis();
        if (ble_is_connected() && (tnow - last_telemetry_time >= TELEMETRY_INTERVAL_MS)) {
            msg_send_telemetry(&sense,
                               device_state(),
                               logic_is_running(&s_model),
                               model_duty(),
                               motor_get());
            last_telemetry_time = tnow;
        }

        // 6. 시리얼 디버그 메시지 방출 (호흡 검출 이벤트 등)
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
