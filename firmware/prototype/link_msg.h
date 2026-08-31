// 출력 계층 — 바깥으로 나가는 문자열을 여기서만 만든다.
//
// 시리얼 모니터로 본 것과 앱이 받은 것이 같아야 디버깅 비용이 반으로 준다.
// 그래서 형식을 만드는 곳은 한 곳이고, 나가는 곳만 둘로 나뉜다:
//
//     msg_report / msg_ack / ...  ──▶  msg_emit(sink, line) ──┬──▶ Serial
//                                                             └──▶ BLE notify
//
// 시리얼 규약 — 형식이 바뀌면 호스트 도구가 깨진다. 문자열을 손대지 말 것:
//   raw<TAB>mv                                  측정 샘플 (필드 2개)
//   # fs=50.00Hz avg=... min=... max=...        표본화 진단
//   # SETTLED                                   정착 완료, 판정 시작
//   # EXHALE n=12345 delay=220ms bpm=15.2       호기 시작 확정
//   # INHALE n=12290 delay=200ms bpm=15.2       흡기 시작 확정
//   # NOSIG amp=3.2  /  # SIGOK amp=8.1         무신호 진입·복귀
// io_serial.parse_sample() 이 필드 2개일 때만 통과시키므로 '#' 줄은 자동으로 걸러진다.
//
// 여기 있는 함수는 전부 app_task(core 0)에서만 부른다. Serial 과 BLE 는 블로킹할 수
// 있으므로 센서 태스크에서 부르면 20ms 주기가 깨진다.

#ifndef LINK_MSG_H
#define LINK_MSG_H

#include "app_types.h"

// 어디로 보낼지. 샘플 줄처럼 50Hz 로 나가는 것은 절대 BLE 로 보내지 않는다
// (BLE 연결 간격이 30~50ms라 물리적으로 초당 20~30개가 상한).
typedef enum {
    SINK_SERIAL = 1 << 0,
    SINK_BLE    = 1 << 1,
    SINK_BOTH   = SINK_SERIAL | SINK_BLE,
} MsgSink;

// 한 줄을 지정한 곳으로 내보낸다. 줄바꿈은 이 안에서 붙인다.
void msg_emit(MsgSink to, const char *line);
void msg_emitf(MsgSink to, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// 한 틱치 스냅샷을 규약대로 출력한다. 줄 순서는 0단계와 같다.
void msg_report(const SenseUpdate *sense);

// 센서 태스크에서 SENSE_STALL_MS 동안 샘플이 오지 않았다.
void msg_sense_stall();

// --- 명령 응답 ---
// 앱 UI 가 낙관적 업데이트에 의존하지 않도록 모든 명령에 답한다.
void msg_ack(CmdSource src, const Command *c);          // "OK MOTOR ON"
void msg_ack_err(CmdSource src, const char *why);       // "ERR unknown"

// 연결 직후 / CMD_STATUS 응답. 앱이 재연결했을 때 현재 상태를 즉시 그릴 수 있어야 한다.
void msg_snapshot(const SenseUpdate *sense, bool motor_on, uint8_t duty);

#endif  // LINK_MSG_H
