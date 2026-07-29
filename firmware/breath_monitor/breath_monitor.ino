// 전도성 고무 호흡 센서 — 기기에서 직접 흡기/호기를 검출하는 펌웨어
//
// conductive_rubber_cord.ino 는 샘플만 흘려보내고 판정은 PC(monitor_breath.py)가 했다.
// 이 스케치는 대역통과와 검출까지 기기에서 하고, 전환 순간을 시리얼로 알린다.
//
// 시리얼 규약 — 기존 PC 도구를 깨지 않는다:
//   1116\t958                                  측정 샘플 (필드 2개)
//   # fs=50.00Hz avg=... min=... max=...        표본화 진단
//   # SETTLED                                   정착 완료, 판정 시작
//   # EXHALE n=12345 delay=220ms bpm=15.2       호기 시작 확정
//   # INHALE n=12290 delay=200ms bpm=15.2       흡기 시작 확정
//   # NOSIG amp=3.2  /  # SIGOK amp=8.1         무신호 진입·복귀
// io_serial.parse_sample() 이 필드 2개일 때만 통과시키므로 '#' 줄은 자동으로 걸러진다.
//
// 검출 로직은 breath/ 패키지의 이식이며, 호스트에서 녹음 CSV 로 대조 검증했다
// (46개 이벤트, 종류 불일치 0, 최대 1샘플 차이. float 와 double 결과 동일).

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "breath_config.h"
#include "breath_filter.h"
#include "breath_slope.h"

const int SENSOR_PIN = 4;        // ADC1 채널 GPIO. WiFi 쓸 거면 반드시 ADC1(GPIO 1~10)
const uint32_t PERIOD_MS = 20;   // 표본화 주기 → 50Hz. breath_config.h 의 FS_HZ 와 짝
const int AVG_N = 16;            // mV 평균 횟수(약 1.9ms). 줄이면 기울기 검출의 노이즈 여유가 준다
const bool REPORT_RATE = true;   // 1초마다 '# fs=...' 진단 줄. 측정이 끝나면 false 로

static_assert(PERIOD_MS * (uint32_t)FS_HZ == 1000,
              "PERIOD_MS 와 breath_config.h 의 FS_HZ 가 어긋납니다");

TickType_t nextWake;   // 다음에 깨어날 절대 시각(틱). loop() 호출 간에 누적된다

static BandpassButter2 bp;
static SlopeDetector det;
static bool announced_settled = false;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);                       // 기본 12비트(0~4095)
  analogSetPinAttenuation(SENSOR_PIN, ADC_6db);  // 0~1750mV (ESP32-S3)

  bandpass_init(&bp, FS_HZ, HP_HZ, LP_HZ);
  slope_init(&det);

  nextWake = xTaskGetTickCount();                 // 타이밍 기준점
}

int readAveragedMv(int pin, int n = AVG_N) {
  uint32_t sum = 0;
  for (int i = 0; i < n; i++) sum += analogReadMilliVolts(pin);
  return sum / n;
}

// 실제 표본화 주기를 스스로 재서 1초에 한 번 보고한다.
// avg 가 PERIOD_MS 에 붙고 min/max 가 avg 근처면 정상.
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

// 지연은 극점 → 확정까지 걸린 샘플 수. 오프라인 지표와 같은 값이다.
void reportEvent(const BreathEvent &ev) {
  const unsigned long delay_ms = (unsigned long)(ev.n - ev.ext_n) * PERIOD_MS;
  const float bpm = (float)slope_bpm(&det);
  switch (ev.type) {
    case BR_INHALE_ONSET:
    case BR_EXHALE_ONSET:
      Serial.printf("# %s n=%lu delay=%lums", ev.type == BR_INHALE_ONSET ? "INHALE" : "EXHALE",
                    (unsigned long)ev.n, delay_ms);
      if (bpm > 0.0f) Serial.printf(" bpm=%.1f", bpm);
      Serial.println();
      break;
    case BR_SIGNAL_LOST:
      Serial.printf("# NOSIG n=%lu amp=%.1f\n", (unsigned long)ev.n, (float)ev.amp);
      break;
    case BR_SIGNAL_OK:
      Serial.printf("# SIGOK n=%lu amp=%.1f\n", (unsigned long)ev.n, (float)ev.amp);
      break;
    default:
      break;
  }
}

void loop() {
  // 샘플링 hz 확인 시 아래 줄 주석 해제
  // if (REPORT_RATE) reportRate();

  int raw = analogRead(SENSOR_PIN);
  int mv  = readAveragedMv(SENSOR_PIN);

  // csv로 센서값 저장 시 아래 줄 주석 해제
  // Serial.print(raw); Serial.print('\t'); Serial.println(mv);

  const bfloat y = bandpass_update(&bp, (bfloat)mv);
  BreathEvent ev;
  if (slope_update(&det, y, &ev)) reportEvent(ev);

  if (!announced_settled && slope_settled(&det)) {
    announced_settled = true;
    Serial.println("# SETTLED");
  }

  vTaskDelayUntil(&nextWake, pdMS_TO_TICKS(PERIOD_MS));
}
