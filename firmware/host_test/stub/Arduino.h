#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>
typedef enum { ADC_0db, ADC_2_5db, ADC_6db, ADC_11db } adc_attenuation_t;
struct SerialStub {
  void begin(unsigned long);
  int printf(const char*, ...) __attribute__((format(printf,2,3)));
  void print(int); void print(char); void print(const char*);
  void println(int); void println(const char*); void println();
};
extern SerialStub Serial;
uint16_t analogRead(uint8_t);
uint32_t analogReadMilliVolts(uint8_t);
void analogReadResolution(uint8_t);
void analogSetPinAttenuation(uint8_t, adc_attenuation_t);
unsigned long micros(void);
void delay(uint32_t);
void rgbLedWrite(uint8_t pin, uint8_t r, uint8_t g, uint8_t b);
