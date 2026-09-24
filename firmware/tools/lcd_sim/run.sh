#!/bin/sh
# LCD1602 시뮬레이터 — 펌웨어의 app_logic.cpp 를 PC 에서 그대로 돌려 화면 흐름을 확인한다.
#
#   sh firmware/tools/lcd_sim/run.sh
#
# 저장소 최상위에서 실행할 것. 방향키 = 버튼, Enter = OK, q = 종료.
set -e
H=firmware/host_test
P=firmware/prototype
T=firmware/tools/lcd_sim
O=$(mktemp -d)
trap 'rm -rf "$O"' EXIT
${CXX:-c++} -std=gnu++17 -O2 -Wall -I$H/stub -I$P $T/lcd_sim.cpp $P/app_logic.cpp -o "$O/lcd_sim"
"$O/lcd_sim"
