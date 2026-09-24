#include <cstdarg>
#include <cstdio>
#include <stddef.h>
#include <stdint.h>

#if __has_include("stub/Arduino.h")
#include "stub/Arduino.h"
#include "stub/freertos/FreeRTOS.h"
#include "stub/freertos/queue.h"
#include "stub/freertos/task.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <Arduino.h>
#endif

// ============================================================================
// [Arduino Serial Stub] - PC 호스트 테스트용 가상 시리얼 인터페이스
// ============================================================================
SerialStub Serial;

void SerialStub::begin(unsigned long) {}

int SerialStub::printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int ret = vprintf(format, args);
  va_end(args);
  return ret;
}

void SerialStub::print(int val) { printf("%d", val); }

void SerialStub::print(char c) { putchar(c); }

void SerialStub::print(const char *str) { fputs(str, stdout); }

void SerialStub::println(int val) { printf("%d\n", val); }

void SerialStub::println(const char *str) { printf("%s\n", str); }

void SerialStub::println() { putchar('\n'); }

// ============================================================================
// [Arduino Hardware API Stub] - 하드웨어 의존 함수들의 가상 구현체
// ============================================================================
uint16_t analogRead(uint8_t) { return 0; }

uint32_t analogReadMilliVolts(uint8_t) { return 0; }

void analogReadResolution(uint8_t) {}

void analogSetPinAttenuation(uint8_t, adc_attenuation_t) {}

unsigned long micros(void) { return 0; }

void delay(uint32_t) {}

// ============================================================================
// [FreeRTOS Stub] - FreeRTOS 태스크 및 큐 모의 동작 구현체
// ============================================================================
TickType_t xTaskGetTickCount(void) { return 0; }

void vTaskDelayUntil(TickType_t *, TickType_t) {}

void vTaskDelay(TickType_t) {}

BaseType_t xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t,
                                   void *, unsigned, TaskHandle_t *, int) {
  return 1;
}

QueueHandle_t xQueueCreate(unsigned, unsigned) { return (QueueHandle_t)1; }

BaseType_t xQueueSend(QueueHandle_t, const void *, TickType_t) {
  return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t) { return pdTRUE; }

// 전역 큐 핸들
QueueHandle_t q_sense = nullptr;
QueueHandle_t q_cmd = nullptr;

// ============================================================================
// [BLE 전송 계층 Stub] - 호스트 환경에서는 실제 BLE HW가 없으므로 No-Op 처리
// ============================================================================
#if __has_include("../prototype/link_ble.h")
#include "../prototype/link_ble.h"
#else
#include "link_ble.h"
#endif

void ble_init() {}

void ble_tick() {}

bool ble_is_connected() { return false; }

void ble_send_line(const char *) {}

void ble_send_telemetry(const uint8_t *, size_t) {}

// ============================================================================
// [명령 파서 Stub] - link_msg.cpp의 cmd_name 호출 링크용
// ============================================================================
#if __has_include("../prototype/link_cmd.h")
#include "../prototype/link_cmd.h"
#else
#include "link_cmd.h"
#endif

const char *cmd_name(CmdType) { return "?"; }
