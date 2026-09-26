#include <Arduino.h>
#include <string.h>
#include "app_types.h"
#include "task_sense.h"
#include "link_msg.h"
#include "csv.h"

static void replay(const char *path, SenseUpdate &u) {
  for (auto &r : read_csv(path)) {
    sense_step((int16_t)r.raw, (int16_t)r.mv, &u);
    msg_report(&u);                 // 큐를 통과한 셈치고 바로 소비
    u.events = 0; u.rate_ready = 0; u.drops = 0;
  }
}

// 사용법: drv_new <csv> [twice]
//   twice — 끝까지 재생한 뒤 센서 태스크가 하는 것과 똑같이 초기화하고(epoch 1) 한 번 더 재생한다.
//           두 번째 결과가 첫 번째와 같아야 sense_reset() 이 상태를 남김없이 지운 것이다.
int main(int argc, char **argv) {
  sense_reset();
  SenseUpdate u = {};
  replay(argv[1], u);

  if (argc > 2 && strcmp(argv[2], "twice") == 0) {
    sense_reset();
    u = {};
    u.epoch = 1;
    replay(argv[1], u);
  }
  return 0;
}
