/**
 * ============================================================================
 * [link_ble.cpp] BLE 전송 계층 구현체 (NimBLE-Arduino 기반)
 * ============================================================================
 *
 * 역할:
 *   - NimBLE GATT Server 운영 (Advertising, Service, Characteristic)
 *   - 앱으로부터 8바이트 제어 명령 수신 (Write) -> Command 큐(q_cmd)로 전달
 *   - 12바이트 텔레메트리 패킷(Notify) 및 진단 텍스트 전송
 *
 * 주요 규칙:
 *   - BLE 콜백 함수 내에서는 절대 모터나 센서 하드웨어 상태를 직접 제어하지 않음.
 *   - 모든 수신 데이터는 cmd_parse_packet() 거쳐 cmd_submit()을 통해 app_task로 전달됨.
 *   - 센서 루프(Core 1) 방해를 막기 위해 본딩(Bonding)은 비활성화 상태 유지.
 * ============================================================================
 */

#include "link_ble.h"

#include <NimBLEDevice.h>

#include "app_types.h"
#include "link_cmd.h"

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

// BLE 스택(Core 0) 콜백이 쓰고 app_task(Core 0)가 읽는 단일 플래그
static volatile bool s_ble_connected   = false;
static volatile bool s_want_advertise  = false;

// ============================================================================
// [GATT Server 콜백 클래스] - 연결 / 해제 이벤트 처리
// ============================================================================
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer) override {
        (void)pServer;
        s_ble_connected = true;

        // 연결 이벤트 알림 명령을 app_task로 전송 (스냅샷 회신 목적)
        Command cmd = { CMD_BLE_CONNECTED, SRC_BLE, 0 };
        cmd_submit(&cmd);
    }

    void onDisconnect(NimBLEServer *pServer) override {
        (void)pServer;
        s_ble_connected = false;
        s_want_advertise = true; // 재광고는 콜백이 아닌 ble_tick()에서 안전하게 수행

        // 연결 끊김 이벤트 명령을 app_task로 전송 (Fail-Safe 긴급 정지 트리거)
        Command cmd = { CMD_BLE_DISCONNECTED, SRC_BLE, 0 };
        cmd_submit(&cmd);
    }
};

// ============================================================================
// [GATT Characteristic 콜백 클래스] - 앱 Write 명령 수신 처리
// ============================================================================
class RxWriteCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) override {
        std::string rxData = pCharacteristic->getValue();
        const uint8_t *pkt = (const uint8_t *)rxData.data();
        size_t len = rxData.length();

        // 8바이트 바이너리 패킷 파싱 후 큐에 전달
        Command cmd = {};
        if (cmd_parse_packet(pkt, len, &cmd)) {
            cmd.src = SRC_BLE;
            cmd_submit(&cmd);
        }
        // *주의*: 파싱 실패 시 응답(ACK/ERR) 및 하드웨어 제어는 여기서 하지 않고 app_task가 전담.
    }
};

// ============================================================================
// [외부 인터페이스 함수 구현]
// ============================================================================

void ble_init() {
    // 1. NimBLE 디바이스 초기화 및 송신 출력 설정 (+9dBm)
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setMTU(185);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

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

    // 5. RX Characteristic (Write / WriteNR, App -> ESP32) 생성 및 콜백 등록
    NimBLECharacteristic *pRxChar = pService->createCharacteristic(
        RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pRxChar->setCallbacks(new RxWriteCallbacks());

    // 6. Service 시작
    pService->start();

    // 7. Advertising 시작
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SVC_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
}

void ble_tick() {
    // 연결 해제 후 재광고 요청 처리 (Non-blocking)
    if (s_want_advertise) {
        s_want_advertise = false;
        NimBLEDevice::startAdvertising();
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
