#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cstdio>
#include <cstdarg>
SerialStub Serial;
void SerialStub::begin(unsigned long) {}
int  SerialStub::printf(const char *f, ...) { va_list a; va_start(a,f); int r=vprintf(f,a); va_end(a); return r; }
void SerialStub::print(int v)          { printf("%d", v); }
void SerialStub::print(char c)         { putchar(c); }
void SerialStub::print(const char *s)  { fputs(s, stdout); }
void SerialStub::println(int v)        { printf("%d\n", v); }
void SerialStub::println(const char *s){ printf("%s\n", s); }
void SerialStub::println()             { putchar('\n'); }
uint16_t analogRead(uint8_t) { return 0; }
uint32_t analogReadMilliVolts(uint8_t) { return 0; }
void analogReadResolution(uint8_t) {}
void analogSetPinAttenuation(uint8_t, adc_attenuation_t) {}
unsigned long micros(void) { return 0; }
void delay(uint32_t) {}
TickType_t xTaskGetTickCount(void) { return 0; }
void vTaskDelayUntil(TickType_t*, TickType_t) {}
void vTaskDelay(TickType_t) {}
BaseType_t xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t,void*,unsigned,TaskHandle_t*,int){return 1;}
QueueHandle_t xQueueCreate(unsigned,unsigned){return (QueueHandle_t)1;}
BaseType_t xQueueSend(QueueHandle_t,const void*,TickType_t){return pdTRUE;}
BaseType_t xQueueReceive(QueueHandle_t,void*,TickType_t){return pdTRUE;}
QueueHandle_t q_sense = nullptr;

// --- BLE 전송 계층 스텁 ---
// 호스트 검증은 Serial 출력만 대조한다. link_ble.cpp 는 <BLEDevice.h> 가 필요해
// 여기 넣지 않고, 인터페이스만 비워 둔다.
#include "link_ble.h"
void ble_init() {}
void ble_tick() {}
bool ble_is_connected() { return false; }
void ble_send_line(const char *) {}
QueueHandle_t q_cmd = nullptr;
void rgbLedWrite(uint8_t, uint8_t, uint8_t, uint8_t) {}
