// 5방향 스위치 배선 확인 — 일회용 도구 (본 펌웨어와 무관한 독립 스케치)
//
// "버튼을 누르면 핀이 LOW 가 되는가, HIGH 가 되는가" 를 알아낸다.
// 본 펌웨어(firmware/prototype)는 "누르면 LOW (COM = GND, 내부 풀업)" 를 전제로 한다.
//
// 사용법
//   1. 이 스케치를 올리고 시리얼 모니터 115200 을 연다.
//   2. 처음에는 풀업 모드다. 버튼을 하나씩 누르고 떼 본다.
//   3. 시리얼에 d 를 입력하면 풀다운 모드로 바뀐다. 다시 하나씩 누르고 떼 본다. (u 로 풀업 복귀)
//   4. 핀 값이 바뀔 때마다 한 줄씩 찍힌다. 1초마다 "살아 있음" 줄도 찍힌다.
//
// 결과 읽는 법
//   풀업 모드에서 누를 때 L        → 누르면 LOW.  본 펌웨어 그대로 맞다.
//   풀다운 모드에서 누를 때 H      → 누르면 HIGH. 본 펌웨어 설정을 바꿔야 한다.
//   두 모드 모두 누르고 떼도 그대로 → 그 버튼의 배선이 끊겼거나 COM 이 연결되지 않았다.
//   떼었을 때 모드와 상관없이 늘 같은 값 → 기판에 외부 저항이 달려 있다(그 값이 "뗌" 레벨).
//
// 다 보고 나면 본 펌웨어(firmware/prototype)를 다시 올릴 것.

#include <Arduino.h>

// board_config.h 핀 배치와 같게 둔다 (스케치 폴더 밖 헤더는 Arduino 빌드에서 못 읽는다).
static const struct { uint8_t pin; const char *name; } KEYS[] = {
    { 10, "RIGHT" },
    { 11, "OK"    },
    { 12, "DOWN"  },
    { 13, "UP"    },
    { 14, "LEFT"  },
};
static const int N = sizeof KEYS / sizeof KEYS[0];

static bool     pulldown = false;
static uint8_t  last[N];
static uint32_t last_beat = 0;

static void set_mode(bool down) {
    pulldown = down;
    for (int i = 0; i < N; i++) pinMode(KEYS[i].pin, down ? INPUT_PULLDOWN : INPUT_PULLUP);
    delay(5);                                   // 풀 저항이 핀을 끌어올리거나 내릴 시간
    for (int i = 0; i < N; i++) last[i] = digitalRead(KEYS[i].pin);
    Serial.printf("\n=== %s 모드 — 버튼을 하나씩 누르고 떼 보세요 (%c 로 전환) ===\n",
                  down ? "풀다운" : "풀업", down ? 'u' : 'd');
}

static void print_all(const char *why) {
    Serial.printf("%-8s %s  ", why, pulldown ? "[풀다운]" : "[풀업]  ");
    for (int i = 0; i < N; i++) Serial.printf(" %s(%d)=%c", KEYS[i].name, KEYS[i].pin, last[i] ? 'H' : 'L');
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(1500);                                // 시리얼 모니터가 붙을 시간
    Serial.println("\n# key_probe — 5방향 스위치 배선 확인");
    set_mode(false);
    print_all("시작");
}

void loop() {
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == 'd' || c == 'D') { set_mode(true);  print_all("시작"); }
        if (c == 'u' || c == 'U') { set_mode(false); print_all("시작"); }
    }

    bool changed = false;
    for (int i = 0; i < N; i++) {
        const uint8_t v = digitalRead(KEYS[i].pin);
        if (v != last[i]) {
            Serial.printf("  %s(GPIO%d) %c → %c\n", KEYS[i].name, KEYS[i].pin, last[i] ? 'H' : 'L', v ? 'H' : 'L');
            last[i] = v;
            changed = true;
        }
    }
    if (changed) print_all("현재");

    if (millis() - last_beat >= 1000) {         // 시리얼이 살아 있는지 보이게
        last_beat = millis();
        print_all("살아있음");
    }
    delay(10);                                  // 배선 확인용 도구라 디바운스는 하지 않는다 — 떨림도 그대로 보인다
}
