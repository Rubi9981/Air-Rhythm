#include <Arduino.h>
#include "app_types.h"
#include "task_sense.h"
#include "link_msg.h"
#include "csv.h"
int main(int argc, char **argv) {
  sense_reset();
  SenseUpdate u = {};
  for (auto &r : read_csv(argv[1])) {
    sense_step((int16_t)r.raw, (int16_t)r.mv, &u);
    msg_report(&u);                 // 큐를 통과한 셈치고 바로 소비
    u.events = 0; u.rate_ready = 0; u.drops = 0;
  }
  return 0;
}
