// 두 코어 사이의 유일한 계약.
//
// 센서 태스크(core 1)와 앱 태스크(core 0)는 진짜로 동시에 실행되므로, 전역 상태를
// 직접 나눠 쓰면 데이터 경쟁이 실제로 발생한다. 그래서 오가는 것을 이 구조체 하나로
// 제한하고 전달은 FreeRTOS 큐로만 한다(큐는 내부 스핀락이 있어 SMP 안전).
//
// 규칙:
//   - 앱 태스크는 SlopeDetector / BandpassButter2 를 절대 직접 읽지 않는다.
//     필요한 값은 센서 태스크가 여기에 "복사"해서 보낸다.
//   - 센서 태스크는 Serial / BLE / 모터를 호출하지 않는다.

#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// SenseUpdate.flags — 매 틱의 상태
#define FLAG_SETTLED    (1u << 0)   // 정착 구간이 끝나 판정이 유효하다
#define FLAG_SIGNAL_OK  (1u << 1)   // 신호가 살아 있다 (진폭이 MIN_AMP 이상)

// SenseUpdate.events — 이번 틱에 확정된 이벤트. 0 이면 없음.
#define EVENT_INHALE       (1u << 0)
#define EVENT_EXHALE       (1u << 1)
#define EVENT_SIGNAL_LOST  (1u << 2)   // 시리얼로는 "# NOSIG" 로 나간다(규약)
#define EVENT_SIGNAL_OK    (1u << 3)   // 시리얼로는 "# SIGOK" 로 나간다(규약)

typedef struct {
    uint32_t n;            // slope_update 후의 detector.n (지금까지 처리한 샘플 수)
    int16_t  raw, mv;      // 이번 샘플의 원시값 (CSV 출력용)

    int8_t   phase;        // BR_RISING=흡기 중 / BR_FALLING=호기 중 / BR_UNKNOWN
    uint8_t  flags;        // FLAG_SETTLED | FLAG_SIGNAL_OK
    uint8_t  events;       // E_* 비트. 0 이면 이벤트 없음
    uint8_t  rate_ready;   // 1 이면 아래 rate_* 가 유효 (약 1초에 한 번)
    uint16_t drops;        // 큐가 차서 버린 틱 수. 정상 동작에서는 항상 0

    // events != 0 일 때만 의미가 있다.
    // ev_n 은 det.n 과 다르다 — slope_update 가 반환 직전에 d->n++ 하기 때문.
    // 지연 계산은 반드시 (ev_n - ext_n) 이어야 한다.
    uint32_t ev_n;         // 이벤트 확정 샘플
    uint32_t ext_n;        // 그 이벤트의 극점 샘플
    float    ev_amp;       // 이벤트 시점의 추정 진폭 (NOSIG/SIGOK 출력용)

    float    amp;          // 현재 추정 진폭(p-p)
    float    bpm;          // 분당 호흡수. 판정 불가면 0

    float    rate_fs;      // 실측 표본화 주파수
    uint32_t rate_avg_us, rate_min_us, rate_max_us;
} SenseUpdate;

// --- 앱 → 기기 방향의 계약 ---
//
// SenseUpdate 가 센서→앱의 유일한 통로이듯, 반대 방향도 통로를 하나만 둔다.
// 시리얼 명령과 BLE write 가 같은 Command 로 수렴하므로, BLE 없이도 전체 명령
// 경로를 시리얼로 테스트할 수 있다.
//
// 연결·해제까지 명령으로 흘려보내는 이유: app_task 가 모든 상태 변화를 한 곳에서
// 같은 방식으로 받게 되고, BLE 콜백이 세우는 전역 플래그가 하나도 생기지 않는다.

typedef enum {
    CMD_NONE = 0,
    CMD_BLE_CONNECTED,      // 콜백이 알린다 → app_task 가 스냅샷을 보낸다
    CMD_BLE_DISCONNECTED,   // 1단계에서 여기에 모터 정지가 붙는다
    CMD_MOTOR_ON,
    CMD_MOTOR_OFF,
    CMD_SET_DUTY,           // arg = 0~255
    CMD_STATUS,             // 현재 상태를 다시 보내달라
} CmdType;

typedef enum {
    SRC_SERIAL = 0,
    SRC_BLE    = 1,
} CmdSource;

typedef struct {
    CmdType   type;
    CmdSource src;   // ACK 를 어디로 보낼지, 로그에 무엇으로 남길지
    int32_t   arg;
} Command;

// --- 큐 ---
extern QueueHandle_t q_sense;   // 센서 → 앱. 매 틱(50Hz) 스냅샷 하나
extern QueueHandle_t q_cmd;     // 시리얼·BLE 콜백 → 앱. 드물게 오는 명령

#endif  // APP_TYPES_H
