#include "link_ble.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "app_types.h"
#include "link_cmd.h"

// TODO(구현): 프로젝트 전용 UUID 로 교체할 것
#define SVC_UUID  "0000fff0-0000-1000-8000-00805f9b34fb"
#define TX_UUID   "0000fff1-0000-1000-8000-00805f9b34fb"   // notify  기기 → 앱
#define RX_UUID   "0000fff2-0000-1000-8000-00805f9b34fb"   // write   앱 → 기기

static BLEServer         *server = nullptr;
static BLECharacteristic *tx_char = nullptr;

// 콜백(BLE 스택 태스크)이 쓰고 ble_tick(app_task)이 읽는다.
// 여기 있는 것만이 이 계층의 유일한 공유 상태이며, 전부 volatile 단일 플래그다.
static volatile bool ble_connected      = false;
static volatile bool want_advertise = false;

// ---------------------------------------------------------------------------
// 콜백 — 파싱과 플래그 세우기만. 여기서 아무것도 소유하지 않는다.
// ---------------------------------------------------------------------------

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *) override {
        ble_connected = true;
        Command cmd = { CMD_BLE_CONNECTED, SRC_BLE, 0 };
        cmd_submit(&cmd);                 // 스냅샷 전송은 app_task 가 한다
    }
    void onDisconnect(BLEServer *) override {
        ble_connected = false;
        want_advertise = true;          // ★ 여기서 startAdvertising() 하지 않는다
        Command cmd = { CMD_BLE_DISCONNECTED, SRC_BLE, 0 };
        cmd_submit(&cmd);
    }
};

class RxWriteCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *characteristic) override {
        std::string rxValue = characteristic->getValue();
        if (rxValue.length() > 0) {
            Command cmd = {};
            if (cmd_parse(rxValue.c_str(), &cmd)) {
                cmd.src = SRC_BLE;
                cmd_submit(&cmd);
            }
        }
    }
};

// ---------------------------------------------------------------------------

void ble_init() {
    BLEDevice::init("ESP32_Vest_BLE");
    BLEDevice::setMTU(185);

    server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    BLEService *pService = server->createService(SVC_UUID);

    tx_char = pService->createCharacteristic(
        TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    tx_char->addDescriptor(new BLE2902());

    BLECharacteristic *rx_char = pService->createCharacteristic(
        RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    rx_char->setCallbacks(new RxWriteCallbacks());

    pService->start();

    // 부팅 직후 블루투스 자동 재연결 대기 시작
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SVC_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
}

void ble_tick() {
    if (want_advertise) {
        want_advertise = false;
        BLEDevice::startAdvertising();
        // 필요하면 해제 직후 잠깐 텀을 두도록 만들 것 (틱 카운터로).
    }
}

bool ble_is_connected() { return ble_connected; }

void ble_send_line(const char *line) {
    if (!ble_connected || !tx_char) return;
    tx_char->setValue((uint8_t*)line, strlen(line));
    tx_char->notify(); // ★ 실제로 앱으로 BLE Notify 전송
}
