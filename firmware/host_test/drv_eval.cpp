// 정확도 평가용 드라이버 — scripts/evaluate.py 가 빌드하고 부른다.
//
// drv_new 와 같은 경로(sense_step = 기기의 센서 태스크가 매 틱 하는 일)로 CSV 를 재생하되,
// 사람이 읽는 "# EXHALE ..." 대신 파이썬이 읽기 쉬운 줄을 찍는다. drv_new 의 출력에는
// 매 샘플의 위상이 없다(UNKNOWN → 흡기/호기 는 이벤트 없이 바뀐다). "흡기 중 타진" 을
// 재려면 위상이 샘플 단위로 필요하므로 따로 둔다.
//
// 출력 (한 줄에 하나, 위상·플래그가 바뀌거나 이벤트가 난 샘플만):
//   C,<이름>=<값>,...          맨 처음 한 번. 실제로 컴파일된 조정값 (-D 가 먹었는지 확인용)
//   S,n,phase,flags,events,ext_n,bpm
//       phase  BR_RISING(+1) / BR_FALLING(-1) / BR_UNKNOWN(0)
//       flags  FLAG_SETTLED | FLAG_SIGNAL_OK
//       events EVENT_* 비트. 0 이면 이 줄은 상태 변화만 알린다
//       ext_n  events 가 흡기/호기일 때 그 극점 샘플. 아니면 0
//   E,n                        마지막 줄. 재생한 샘플 수
//
//   drv: drv_eval <csv>

#include <stdio.h>

#include <Arduino.h>
#include "app_types.h"
#include "breath_config.h"
#include "csv.h"
#include "task_sense.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: drv_eval <csv>\n"); return 2; }

    printf("C,HP_HZ=%g,LP_HZ=%g,SLOPE_TAU_S=%g,AVG_TAU_S=%g,K_SLOPE=%g,SLOPE_FLOOR=%g,"
           "MIN_PHASE_S=%g,MID_GATE=%d,PROM_RATIO=%g,MIN_AMP=%g,ENV_DECAY_S=%g,SETTLE_S=%g,"
           "POLARITY=%g,NO_SIGNAL_HYST=%g\n",
           (double)HP_HZ, (double)LP_HZ, (double)SLOPE_TAU_S, (double)AVG_TAU_S,
           (double)K_SLOPE, (double)SLOPE_FLOOR, (double)MIN_PHASE_S, (int)MID_GATE,
           (double)PROM_RATIO, (double)MIN_AMP, (double)ENV_DECAY_S, (double)SETTLE_S,
           (double)POLARITY, (double)NO_SIGNAL_HYST);

    sense_reset();
    SenseUpdate u = {};
    int8_t prev_phase = 127;         // 첫 샘플을 반드시 찍게 하는 불가능한 값
    uint8_t prev_flags = 0xFF;
    unsigned long n = 0;
    for (auto &r : read_csv(argv[1])) {
        sense_step((int16_t)r.raw, (int16_t)r.mv, &u);
        if (u.events || u.phase != prev_phase || u.flags != prev_flags) {
            const bool onset = u.events & (EVENT_INHALE | EVENT_EXHALE);
            printf("S,%lu,%d,%u,%u,%lu,%.2f\n", n, (int)u.phase, (unsigned)u.flags,
                   (unsigned)u.events, onset ? (unsigned long)u.ext_n : 0ul, (double)u.bpm);
            prev_phase = u.phase;
            prev_flags = u.flags;
        }
        u.events = 0;
        n++;
    }
    printf("E,%lu\n", n);
    return 0;
}
