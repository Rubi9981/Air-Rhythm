/**
 * ============================================================================
 * [link_ble.cpp] BLE 전송 계층 구현체 (NimBLE-Arduino 기반)
 * ============================================================================
 *
 * 역할:
 *   - NimBLE GATT Server 운영 (Advertising, Service, Characteristic)
 *   - 12바이트 텔레메트리 패킷(Notify) 및 진단 텍스트 전송
 *   - 모니터링 전용: 앱에서 오는 Write 는 받기만 하고 무시한다. 모터를 켜는 경로는
 *     기기 버튼 하나뿐이어야 화면과 실제 동작이 어긋나지 않는다.
 *
 * 필요 라이브러리:
 *   NimBLE-Arduino (h2zero) 2.x — 검증에 쓴 버전은 2.5.1.
 *   1.x 와는 API 가 호환되지 않는다. 1.x 로 빌드하면 컴파일이 깨진다:
 *     - 2.x 의 onConnect/onDisconnect/onWrite 는 NimBLEConnInfo& 를 함께 받는다
 *     - setScanResponse() 가 enableScanResponse() 로 바뀌었다
 *     - setPower() 가 ESP_PWR_LVL_* 열거형이 아니라 dBm 정수를 받는다
 *   라이브러리 매니저에서 "NimBLE-Arduino" 설치. ESP32 코어 3.3.11 에서 확인.
 *   (docs/1.x_to2.x_migration_guide.md 가 라이브러리에 같이 들어 있다)
 *
 * 주요 규칙:
 *   - BLE 콜백 함수 내에서는 절대 모터나 센서 하드웨어 상태를 직접 제어하지 않음.
 *   - 연결 끊김은 모터와 무관하다 — 휴대폰이 멀어졌다고 버튼으로 시작한 타진이 멈추면 안 된다.
 *   - 센서 루프(Core 1) 방해를 막기 위해 본딩(Bonding)은 비활성화 상태 유지.
 * ============================================================================
 */

#include "link_ble.h"

#include <NimBLEDevice.h>
#include <string>

#include "app_types.h"
#include "link_cmd.h"
#include "link_msg.h"

// ============================================================================
// [BLE UUID 및 설정 정의] - Android 앱 (BleUuids)과 100% 일치
// ============================================================================
#define DEVICE_NAME  "RespiSync_Vest"
#define SVC_UUID     "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define TX_UUID      "ba000001-36e1-4688-b7f5-ea07361b26a8"   // Notify (ESP32 -> App)
#define RX_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"   // Write  (App -> ESP32)

// ============================================================================
// [내부 전역 변수]
// ============================================================================
static NimBLEServer         *s_server  = nullptr;
static NimBLECharacteristic *s_tx_char = nullptr;
static NimBLECharacteristic *s_rx_char  = nullptr;

// BLE 스택(Core 0) 콜백이 쓰고 app_task(Core 0)가 읽는 단일 플래그
static volatile bool s_ble_connected   = false;
static volatile bool s_want_advertise  = false;

// ============================================================================
// [GATT Server 콜백 클래스] - 연결 / 해제 이벤트 처리
// ============================================================================
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override {
        (void)pServer;
        (void)connInfo;
        s_ble_connected = true;

        // 연결 이벤트 알림 명령을 app_task로 전송 (스냅샷 회신 목적)
        Command cmd = { CMD_BLE_CONNECTED, SRC_BLE, 0 };
        cmd_submit(&cmd);
    }

    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override {
        (void)pServer;
        (void)connInfo;
        (void)reason;
        s_ble_connected = false;
        s_want_advertise = true; // 재광고 요청
    }
};

// ============================================================================
// [RX Characteristic 콜백 클래스] - 앱에서 BLE로 전송한 제어 명령 수신
// ============================================================================
class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override {
        (void)connInfo;
        std::string rxVal = pCharacteristic->getValue();
        if (!rxVal.empty()) {
            Command cmd = {};
            if (cmd_parse(rxVal.c_str(), &cmd)) {
                cmd.src = SRC_BLE;
                cmd_submit(&cmd);
            } else {
                msg_ack_err(SRC_BLE, "unknown_cmd");
            }
        }
    }
};

// ============================================================================
// [외부 인터페이스 함수 구현]
// ============================================================================

void ble_init() {
    // 1. NimBLE 디바이스 초기화 및 송신 출력 설정 (+9dBm)
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setMTU(185);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // setPower 함수의 인자로 넣는 열거형 변수 사라짐, 원하는 dB로 단순 숫자 작성

    // *보안 설정*: 본딩 키 기록으로 인한 Flash NVS 쓰기 블로킹(Core 1 정지) 원천 방지
    NimBLEDevice::setSecurityAuth(false, false, false);

    // 2. Server 생성 및 콜백 등록
    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(new ServerCallbacks());

    // 3. Service 생성
    NimBLEService *pService = s_server->createService(SVC_UUID);

    // 4. TX Characteristic (Notify, ESP32 -> App) 생성 (CCCD 자동 등록됨)
    s_tx_char = pService->createCharacteristic(
        TX_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    // 5. RX Characteristic (Write / WriteNR, App -> ESP32)
    //    모니터링 전용이라 콜백을 달지 않는다 — 써도 받기만 하고 버린다. 특성 자체는 남겨
    //    두어 기존 앱이 연결·쓰기에서 오류를 내지 않게 한다.
    pService->createCharacteristic(
        RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );

    // 6. Service 시작
    pService->start();  // 이 줄은 동작 안함

    // 7. Advertising 시작 (자동 재연결 신속화를 위해 간격 20ms~40ms 지정)
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SVC_UUID);
    pAdvertising->enableScanResponse(true);
    pAdvertising->setMinInterval(0x20); // 20ms
    pAdvertising->setMaxInterval(0x40); // 40ms
    pAdvertising->start();
}

void ble_tick() {
    // 연결 해제 후 자동 재광고 처리 (Non-blocking)
    if (s_want_advertise) {
        s_want_advertise = false;
        NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->start();
    }
}

bool ble_is_connected() {
    return s_ble_connected;
}

void ble_send_line(const char *line) {
    if (!s_ble_connected || !s_tx_char || line == nullptr) return;
    s_tx_char->setValue((const uint8_t *)line, strlen(line));
    s_tx_char->notify();
}

void ble_send_telemetry(const uint8_t *pkt, size_t len) {
    if (!s_ble_connected || !s_tx_char || pkt == nullptr || len == 0) return;
    s_tx_char->setValue(pkt, len);
    s_tx_char->notify();
}
