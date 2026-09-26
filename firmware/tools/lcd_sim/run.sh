#!/bin/sh
# LCD1602 시뮬레이터 — 펌웨어의 app_logic.cpp 와 호흡 검출기를 PC 에서 그대로 돌려 화면 흐름을 확인한다.
#
#   sh firmware/tools/lcd_sim/run.sh                 합성 호흡 신호 (분당 15회)
#   sh firmware/tools/lcd_sim/run.sh data/유병오.csv  녹음 재생
#
# 저장소 최상위에서 실행할 것. 방향키 = 버튼, Enter = OK, b = 호흡 켜기/끄기, q = 종료.
set -e
H=firmware/host_test
P=firmware/prototype
T=firmware/tools/lcd_sim
O=$(mktemp -d)
trap 'rm -rf "$O"' EXIT
${CXX:-c++} -std=gnu++17 -O2 -Wall -I$H/stub -I$P -I$H \
    $T/lcd_sim.cpp $H/stub_impl.cpp $P/app_logic.cpp $P/task_sense.cpp \
    $P/breath_filter.cpp $P/breath_slope.cpp -o "$O/lcd_sim"
"$O/lcd_sim" "$@"
