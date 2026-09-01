#include "task_app.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "act_indicator.h"
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

        // 아래 default 로 빠지는 명령은 없다. 새 CmdType 을 추가하면 여기도 늘려야 한다.

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
    while (xQueueReceive(q_cmd, &cmd, 0) == pdTRUE) {
        // 명령이 도착했다는 사실 자체를 LED 로 알린다. 명령의 성패와 무관하게
        // "BLE 신호가 여기까지 왔다" 를 눈으로 확인하는 통로다.
        indicator_flash(FLASH_CMD_OK);
        apply_command(&cmd);
    }
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
            const IndicatorState fault_ind = { 0, ble_is_connected(), false, false, true };
            indicator_show(&fault_ind);
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

        // 액추에이터 출력 — 매 틱 재선언한다. 갱신이 끊기면 꺼지는 성질을
        // 모터와 똑같이 갖게 하려는 것이다. 실제 LED 쓰기는 색이 바뀔 때만 일어난다.
        const IndicatorState ind = {
            .duty       = app_state.motor_on ? app_state.duty : (uint8_t)0,
            .ble_linked = ble_is_connected(),
            .settled    = (sense.flags & FLAG_SETTLED) != 0,
            .signal_ok  = (sense.flags & FLAG_SIGNAL_OK) != 0,
            .fault      = false,   // TODO(1단계): 결함 상태를 app_state 로 들고 올 것
        };
        indicator_show(&ind);

        msg_report(&sense);
    }
}

void app_start() {
    xTaskCreatePinnedToCore(app_task, "app", APP_STACK, nullptr,
                            APP_PRIORITY, nullptr, APP_CORE);
}
