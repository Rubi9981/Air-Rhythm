// 출력 계층 — 시리얼 규약을 여기 한 곳에서만 만든다.
//
// breath_monitor.ino 의 reportRate() / reportEvent() / '# SETTLED' 출력이 그대로
// 옮겨왔다. 형식이 바뀌면 호스트 도구가 깨지므로 문자열을 손대지 말 것:
//   raw<TAB>mv                                  측정 샘플 (필드 2개)
//   # fs=50.00Hz avg=... min=... max=...        표본화 진단
//   # SETTLED                                   정착 완료, 판정 시작
//   # EXHALE n=12345 delay=220ms bpm=15.2       호기 시작 확정
//   # INHALE n=12290 delay=200ms bpm=15.2       흡기 시작 확정
//   # NOSIG amp=3.2  /  # SIGOK amp=8.1         무신호 진입·복귀
// io_serial.parse_sample() 이 필드 2개일 때만 통과시키므로 '#' 줄은 자동으로 걸러진다.
//
// 여기 있는 함수는 전부 앱 태스크(core 0)에서만 부른다. Serial 은 블로킹하므로
// 센서 태스크에서 부르면 20ms 주기가 깨진다.

#ifndef LINK_MSG_H
#define LINK_MSG_H

#include "app_types.h"

// 한 틱치 스냅샷을 규약대로 출력한다. 줄 순서는 기존 loop() 와 같다.
void msg_report(const SenseUpdate *s);

// 센서 태스크에서 SENSE_STALL_MS 동안 샘플이 오지 않았다.
void msg_sense_stall();

#endif  // LINK_MSG_H
