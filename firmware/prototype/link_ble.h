// BLE 전송 계층 — GATT 서버와 바깥 세계 사이의 어댑터.
//
// 이 계층은 판단하지 않는다. 바이트를 보내고, 받은 바이트를 Command 로 바꿔
// q_cmd 에 넣는 것이 전부다. 모드·모터·상태는 app_task 가 소유한다.
//
// --- 배치 (확인된 사실) ---
// arduino-esp32 3.3.11 / ESP32-S3 는 NimBLE 로 빌드돼 있고(Bluedroid 꺼짐),
// sdkconfig 에서 컨트롤러와 호스트가 모두 core 0 에 고정돼 있다:
//     CONFIG_BT_NIMBLE_ENABLED=y
//     CONFIG_BT_CTRL_PINNED_TO_CORE=0
//     CONFIG_BT_NIMBLE_PINNED_TO_CORE=0
// 사전 컴파일 라이브러리라 IDE 에서 바뀌지 않는다. core 1 은 센서 전용으로 안전하다.
// 번들 <BLEDevice.h> API 를 쓰면 밑이 NimBLE 이므로, 별도 라이브러리 설치는
// 중복 스택 위험만 만든다.
//
// --- 지켜야 할 것 ---
//  1. 콜백에서 상태를 바꾸지 않는다. cmd_submit() 으로만 넘긴다.
//  2. 콜백 안에서 재광고를 시작하지 않는다. 플래그만 세우고 ble_tick() 이 처리한다.
//  3. notify 를 50Hz 로 보내지 않는다. 이벤트는 즉시, 상태 요약은 1Hz.
//  4. 본딩을 켜지 않는다 — 본딩 키가 NVS 에 기록되면 플래시 쓰기 동안 캐시가 꺼져
//     core 1 의 센서 태스크가 통째로 멈춘다. 구조로 막을 수 없는 유일한 경로다.

#ifndef LINK_BLE_H
#define LINK_BLE_H

// setup() 에서 한 번. GATT 서버를 세우고 광고를 시작한다.
// q_cmd 가 만들어진 뒤에 부를 것 — 콜백이 바로 들어올 수 있다.
void ble_init();

// app_task 가 매 틱 부른다. 콜백이 미뤄둔 일(재광고 등)을 여기서 처리한다.
// 블로킹하지 않는다.
void ble_tick();

bool ble_is_connected();

// 한 줄을 notify 로 보낸다. 미연결이면 조용히 버린다(no-op).
// 번들 API 의 notify() 는 반환값이 없어 전송 성공을 알 수 없다 —
// 유실 감지는 앱이 seq 번호로 한다(link_msg.h 참조).
void ble_send_line(const char *line);

#endif  // LINK_BLE_H
