// 전도성 고무 호흡 센서 — ESP32-S3 로거
//
// 한 줄에 "raw<TAB>mv" 를 PERIOD_MS 간격으로 보낸다.
//
// 타이밍: delay(20) 은 "작업 후 추가로 20ms 쉬기" 라서 실제 주기가
// (작업시간 + 20ms) 가 된다. ADC 17회(raw 1 + 평균 16)에 약 2ms 가 걸려
// 50Hz 가 아니라 45.5Hz 로 나왔다. vTaskDelayUntil 은 절대 시각 기준이라
// 작업 시간을 주기에서 빼주므로 정확히 PERIOD_MS 간격이 된다.
// (작업이 PERIOD_MS 를 넘기면 따라잡기 위해 연속 실행되니 여유를 둘 것.)
//
// 호스트의 표본화 주파수 설정(fs)은 1000/PERIOD_MS 와 일치시켜야 한다.
// 대역통과 계수가 차단주파수/fs 비율로만 설계되므로, fs 가 어긋나면
// 차단주파수가 같은 비율로 밀린다.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

const int SENSOR_PIN = 4;        // ADC1 채널 GPIO. WiFi 쓸 거면 반드시 ADC1(GPIO 1~10)
const uint32_t PERIOD_MS = 20;   // 표본화 주기 → 50Hz
const int AVG_N = 16;            // mV 평균 횟수(약 1.9ms). 줄이면 기울기 검출의 노이즈 여유가 준다
const bool REPORT_RATE = true;   // 1초마다 '# fs=...' 진단 줄. 측정이 끝나면 false 로

TickType_t nextWake;   // 다음에 깨어날 절대 시각(틱). loop() 호출 간에 누적된다

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);                       // 기본 12비트(0~4095)
  analogSetPinAttenuation(SENSOR_PIN, ADC_6db);  // 0~1750mV (ESP32-S3)
  nextWake = xTaskGetTickCount();                 // 타이밍 기준점
}

int readAveragedMv(int pin, int n = AVG_N) {
  uint32_t sum = 0;
  for (int i = 0; i < n; i++) sum += analogReadMilliVolts(pin);
  return sum / n;
}

// 실제 표본화 주기를 스스로 재서 1초에 한 번 보고한다.
// avg 가 PERIOD_MS 에 붙고 min/max 가 avg 근처면 정상.
// '#' 로 시작하는 줄은 필드가 2개가 아니라서 호스트 parse_line 이 알아서 버린다.
void reportRate() {
  static uint32_t prev = 0, n = 0, sum = 0, p_min = 0xFFFFFFFF, p_max = 0;
  uint32_t now = micros();
  if (prev) {
    uint32_t p = now - prev;    // 부호 없는 뺄셈이라 micros() 순환(약 71분)에도 안전
    sum += p; n++;
    if (p < p_min) p_min = p;
    if (p > p_max) p_max = p;
    if (n >= 1000 / PERIOD_MS) {                  // 약 1초치
      Serial.printf("# fs=%.2fHz avg=%luus min=%luus max=%luus\n",
                    1e6f * n / sum, (unsigned long)(sum / n),
                    (unsigned long)p_min, (unsigned long)p_max);
      n = 0; sum = 0; p_min = 0xFFFFFFFF; p_max = 0;
    }
  }
  prev = now;
}

void loop() {
  if (REPORT_RATE) reportRate();

  int raw = analogRead(SENSOR_PIN);
  int mv  = readAveragedMv(SENSOR_PIN);
  Serial.print(raw); Serial.print('\t'); Serial.println(mv);  // Serial Plotter 비교용

  vTaskDelayUntil(&nextWake, pdMS_TO_TICKS(PERIOD_MS));
}