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
        // TODO(구현):
        //   1. characteristic->getValue() 를 널 종단 문자열로 복사 (길이 상한을 둘 것)
        //   2. cmd_parse() 로 Command 로 바꾼다
        //   3. src = SRC_BLE 로 채우고 cmd_submit()
        //   4. 파싱 실패해도 여기서 응답하지 않는다 — ACK 는 app_task 담당
        // 이 함수 안에서 모드·모터·전역 상태를 건드리지 말 것.
        (void)characteristic;
    }
};

// ---------------------------------------------------------------------------

void ble_init() {
    // TODO(구현):
    //   BLEDevice::init("...");  BLEDevice::setMTU(185);
    //   server = BLEDevice::createServer();  server->setCallbacks(new ServerCallbacks());
    //   서비스 생성 → tx_char(NOTIFY) / rx(WRITE) 특성 추가 → rx->setCallbacks(new RxWriteCallbacks())
    //   tx_char->addDescriptor(new BLE2902());
    //   서비스 start() → 광고 파라미터 설정 → BLEDevice::startAdvertising()
    //
    //   보안: 본딩을 켜지 말 것 (헤더 주석 4번). 페어링 없이 열어둔다.
}

void ble_tick() {
    if (want_advertise) {
        want_advertise = false;
        // TODO(구현): BLEDevice::startAdvertising();
        // 필요하면 해제 직후 잠깐 텀을 두도록 만들 것 (틱 카운터로).
    }
}

bool ble_is_connected() { return ble_connected; }

void ble_send_line(const char *line) {
    if (!ble_connected || !tx_char) return;      // 미연결이면 조용히 버린다
    // TODO(구현): tx_char->setValue((uint8_t*)line, strlen(line)); tx_char->notify();
    //   호출자가 이미 sink 를 골라 부르므로 여기서 빈도 제한을 하지 않는다.
    (void)line;
}
