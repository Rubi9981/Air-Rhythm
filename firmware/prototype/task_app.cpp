#include "task_app.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_types.h"
#include "board_config.h"
#include "link_ble.h"
#include "link_cmd.h"
#include "link_msg.h"

// ---------------------------------------------------------------------------
// 앱 태스크가 소유하는 상태. 다른 태스크·콜백은 절대 직접 만지지 않는다.
// 바꾸려면 q_cmd 로 Command 를 보내야 한다.
// ---------------------------------------------------------------------------
typedef struct {
    bool    motor_on;
    uint8_t duty;
    // 1단계에서 추가: 결함 상태, 모드 비트마스크, 최대 연속 동작 타이머
} AppState;

static AppState    app_state    = { false, 0 };
static SenseUpdate latest_sense = {};   // 스냅샷 응답용 — 마지막으로 받은 센서 상태

// ---------------------------------------------------------------------------

static void apply_command(const Command *cmd) {
    switch (cmd->type) {
        case CMD_BLE_CONNECTED:
            msg_snapshot(&latest_sense, app_state.motor_on, app_state.duty);  // 앱이 현재 상태를 즉시 그린다
            break;

        case CMD_BLE_DISCONNECTED:
            // TODO(1단계): 연결이 끊기면 모터를 멈출지 정책 결정.
            //   신체 접촉 액추에이터이므로 "끊기면 정지" 가 안전한 기본값이다.
            break;

        case CMD_MOTOR_ON:  app_state.motor_on = true;  msg_ack(cmd->src, cmd); break;
        case CMD_MOTOR_OFF: app_state.motor_on = false; msg_ack(cmd->src, cmd); break;

        // 범위 검사는 cmd_parse() 가 이미 했다 — 잘못된 값은 여기까지 오지 않는다.
        case CMD_SET_DUTY:
            app_state.duty = (uint8_t)cmd->arg;
            msg_ack(cmd->src, cmd);
            break;

        case CMD_STATUS:
            msg_snapshot(&latest_sense, app_state.motor_on, app_state.duty);
            break;

        default:
            msg_ack_err(cmd->src, "unknown");
            break;
    }
}

// 명령은 드물게 오므로 한 틱에 남은 것을 전부 비운다.
static void drain_commands() {
    Command cmd;
    while (xQueueReceive(q_cmd, &cmd, 0) == pdTRUE) apply_command(&cmd);
}

// ---------------------------------------------------------------------------

static void app_task(void *) {
    SenseUpdate sense;

    for (;;) {
        // 타임아웃이 곧 데드맨이다. 센서 태스크가 멈추면 여기서 잡힌다.
        // 정상이면 20ms 마다 오므로 SENSE_STALL_MS 까지 갈 일이 없다.
        // 주의: 큐가 "가득 찬" 상황은 여기서 안 걸린다 — 데이터가 있으므로 즉시 반환한다.
        //       그건 # QDROP 으로 드러난다. 둘은 서로 다른 고장이다.
        if (xQueueReceive(q_sense, &sense, pdMS_TO_TICKS(SENSE_STALL_MS)) != pdTRUE) {
            msg_sense_stall();
            // TODO(1단계): motor_set(0) — 센서가 죽었으면 무조건 정지
            continue;
        }
        latest_sense = sense;

        cmd_poll_serial();   // 시리얼로 들어온 명령 → q_cmd
        drain_commands();     // 시리얼·BLE 명령을 여기서만 반영한다
        ble_tick();          // 콜백이 미뤄둔 일(재광고 등)

        // TODO(1단계): 여기에 기능 판단 → 중재 → 액추에이터가 들어간다.
        //   Intent in = {0};
        //   if (app_state.motor_on) feat_breath(&sense, &in);   // 호기 중이면 타진을 원한다
        //   arbitrate(&in);                            // 안전 검사 후 매 틱 재선언
        //
        //   주의: 큐에 밀린 것이 많을 때는 오래된 phase 로 모터를 켜게 된다.
        //   액추에이터 판단만은 uxQueueMessagesWaiting() 이 0 일 때의 최신 sense 로 할 것.
        //   보고는 밀린 것도 순서대로 다 내보내면 된다.

        msg_report(&sense);
    }
}

void app_start() {
    xTaskCreatePinnedToCore(app_task, "app", APP_STACK, nullptr,
                            APP_PRIORITY, nullptr, APP_CORE);
}
