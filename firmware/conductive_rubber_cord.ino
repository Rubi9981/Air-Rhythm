const int SENSOR_PIN = 4;   // ADC1 채널 GPIO. WiFi 쓸 거면 반드시 ADC1(GPIO 1~10)

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);                       // 기본 12비트(0~4095)
  analogSetPinAttenuation(SENSOR_PIN, ADC_6db);  // 0~1750mV (ESP32-S3)
}

int readAveragedMv(int pin, int n = 16) {
  uint32_t sum = 0;
  for (int i = 0; i < n; i++) sum += analogReadMilliVolts(pin);
  return sum / n;
}

void loop() {
  int raw = analogRead(SENSOR_PIN);
  int mv  = readAveragedMv(SENSOR_PIN);
  Serial.print(raw); Serial.print('\t'); Serial.println(mv);  // Serial Plotter 비교용
  delay(20);  // 50Hz
}