// BLE 전송 계층 — GATT 서버와 바깥 세계 사이의 어댑터.
//
// 이 계층은 판단하지 않는다. 바이트를 보내고, 받은 바이트를 Command 로 바꿔
// q_cmd 에 넣는 것이 전부다. 모드·모터·상태는 app_task 가 소유한다.
//
// --- BLE 스택 ---
// NimBLE-Arduino (h2zero) 라이브러리를 사용한다.
// 컨트롤러와 호스트 모두 core 0 에 고정돼 있어 core 1 센서와 격리된다.
//
// --- 지켜야 할 것 ---
//  1. 콜백에서 상태를 바꾸지 않는다. cmd_submit() 으로만 넘긴다.
//  2. 콜백 안에서 재광고를 시작하지 않는다. 플래그만 세우고 ble_tick() 이 처리한다.
//  3. 본딩을 켜지 않는다 — 본딩 키가 NVS 에 기록되면 플래시 쓰기 동안 캐시가 꺼져
//     core 1 의 센서 태스크가 통째로 멈춘다.

#ifndef LINK_BLE_H
#define LINK_BLE_H

#include <stdint.h>
#include <stddef.h>

// setup() 에서 한 번. NimBLE GATT 서버를 세우고 광고를 시작한다.
// q_cmd 가 만들어진 뒤에 부를 것 — 콜백이 바로 들어올 수 있다.
void ble_init();

// app_task 가 매 틱 부른다. 콜백이 미뤄둔 일(재광고 등)을 여기서 처리한다.
// 블로킹하지 않는다.
void ble_tick();

bool ble_is_connected();

// 한 줄 텍스트를 notify 로 보낸다. 미연결이면 조용히 버린다(no-op).
// 시리얼 디버그 출력과 동일한 형식을 앱에게도 보낼 때 사용.
void ble_send_line(const char *line);

// 12바이트 바이너리 텔레메트리 패킷을 notify 로 보낸다.
// 미연결이면 조용히 버린다.
void ble_send_telemetry(const uint8_t *pkt, size_t len);

#endif  // LINK_BLE_H
