#include "task_app.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_types.h"
#include "board_config.h"
#include "link_msg.h"

static void app_task(void *) {
    SenseUpdate s;

    for (;;) {
        // 타임아웃이 곧 데드맨이다. 센서 태스크가 멈추면 여기서 잡힌다.
        // 정상이면 20ms 마다 오므로 SENSE_STALL_MS(200ms)까지 갈 일이 없다.
        if (xQueueReceive(q_sense, &s, pdMS_TO_TICKS(SENSE_STALL_MS)) != pdTRUE) {
            msg_sense_stall();
            // 1단계에서 여기에 percussion_set(0) 이 들어간다.
            continue;
        }

        msg_report(&s);
    }
}

void app_start() {
    xTaskCreatePinnedToCore(app_task, "app", APP_STACK, nullptr,
                            APP_PRIO, nullptr, APP_CORE);
}
