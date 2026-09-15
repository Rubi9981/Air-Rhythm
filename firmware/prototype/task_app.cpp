#include "task_app.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "act_motor.h"
#include "app_types.h"
#include "board_config.h"
#include "breath_slope.h"   // BR_RISING(흡기) / BR_FALLING(호기) / BR_UNKNOWN
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

// 마지막 게이트 판단의 이유. STATE 줄에 실어 "왜 안 도는지" 를 바로 보이게 한다.
static const char *gate_reason = "off";

// ---------------------------------------------------------------------------
// 중재 — 호기 구간에만 타진한다.
//
// 매 틱 처음부터 다시 판단한다. 이전 틱의 결정을 물려받지 않으므로 상태가
// 어긋난 채 남을 수 없다. 이벤트(EVENT_EXHALE/EVENT_INHALE)가 아니라 위상
// (phase)을 보는 이유가 이것이다 — 이벤트는 한 번 놓치면 그 틱의 정보가
// 영영 사라지지만, 위상은 매 틱 다시 실려 오므로 다음 틱에 저절로 복구된다.
// 큐 드롭(# QDROP)이 실제로 일어날 수 있는 구조라 이 차이가 중요하다.
//
// 반환 0 은 "지금은 때리지 않는다" 이고, 그 이유를 gate_reason 에 남긴다.
// ---------------------------------------------------------------------------
static uint8_t decide_duty(const SenseUpdate *sense, bool backlog) {
    // 명령으로 꺼져 있다.
    if (!app_state.motor_on) { gate_reason = "off"; return 0; }

    // 큐에 밀린 것이 있다 = 지금 든 sense 가 최신이 아니다.
    // q_sense 는 32칸이라 최악의 경우 640ms 묵은 위상이고, 호흡 한 주기가
    // 3~4초이므로 흡기·호기가 뒤집히기에 충분하다. 묵은 판단으로 때리지 않는다.
    if (backlog) { gate_reason = "backlog"; return 0; }

    // 정착 전에는 위상 판정 자체가 유효하지 않다(SETTLE_S = 12초).
    if (!(sense->flags & FLAG_SETTLED)) { gate_reason = "settling"; return 0; }

    // 스트랩이 떨어졌거나 진폭이 MIN_AMP 아래다. 위상은 잡음일 뿐이다.
    if (!(sense->flags & FLAG_SIGNAL_OK)) { gate_reason = "nosig"; return 0; }

    // 흡기 중에는 절대 때리지 않는다. 이 프로젝트의 핵심 요구사항이다.
    if (sense->phase != BR_FALLING) { gate_reason = "inhale"; return 0; }

    gate_reason = "run";
    return app_state.duty;
}

// ---------------------------------------------------------------------------

static void apply_command(const Command *cmd) {
    switch (cmd->type) {
        case CMD_BLE_CONNECTED:
            msg_snapshot(&latest_sense, app_state.motor_on, app_state.duty,
                         motor_get(), gate_reason);  // 앱이 현재 상태를 즉시 그린다
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
            msg_snapshot(&latest_sense, app_state.motor_on, app_state.duty,
                         motor_get(), gate_reason);
            break;

        // 배선 검증용. 창(窓) 동안 이 루프가 멈추므로 24V 를 넣기 전에만 쓴다.
        case CMD_SELFTEST:
            motor_selftest();
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
            motor_set(0);          // 센서가 죽었으면 무조건 정지
            continue;
        }
        latest_sense = sense;

        cmd_poll_serial();   // 시리얼로 들어온 명령 → q_cmd
        drain_commands();     // 시리얼·BLE 명령을 여기서만 반영한다
        ble_tick();          // 콜백이 미뤄둔 일(재광고 등)

        // 중재 → 액추에이터. 판단은 "지금 든 sense 가 최신일 때" 만 유효하므로
        // 큐에 밀린 것이 있는지 먼저 본다. 보고(msg_report)는 밀린 것도 순서대로
        // 다 내보내면 되지만, 모터 판단은 최신 위상으로만 해야 한다.
        const bool backlog = uxQueueMessagesWaiting(q_sense) > 0;

        // 매 틱 재선언한다. 이 호출이 끊기면 모터가 저절로 멈추므로
        // 루프 구조 자체가 워치독이 된다(act_motor.h 참조).
        motor_set(decide_duty(&sense, backlog));

        // TODO(다음): 최대 연속 동작 타이머, 기동·정지 지연 보정.
        //   BLDC 는 정지에 시간이 걸리므로, 정지가 느리면 호기가 끝나기 전에
        //   미리 꺼야 흡기 구간을 침범하지 않는다. 실측값이 나오면 여기에 붙인다.

        msg_report(&sense);
    }
}

void app_start() {
    xTaskCreatePinnedToCore(app_task, "app", APP_STACK, nullptr,
                            APP_PRIORITY, nullptr, APP_CORE);
}
