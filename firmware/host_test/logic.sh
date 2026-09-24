#!/bin/sh
# 화면 상태 머신(app_logic) 검증 — 모터가 언제 돌고 서는지 두 가지로 확인한다.
#
#   1. logic_test     버튼·센서 샘플 시나리오 단위 테스트
#   2. breath_replay  녹음 CSV 를 실제 검출기 + 상태 머신에 흘려 호흡 모드 안전 규칙 확인
#
#   sh firmware/host_test/logic.sh
#
# 저장소 최상위에서 실행할 것. app_logic.cpp 를 고치면 반드시 돌린다.
set -e
H=firmware/host_test
P=firmware/prototype
O=$(mktemp -d)
trap 'rm -rf "$O"' EXIT
CXX="${CXX:-c++} -std=gnu++17 -O2 -Wall -Wextra -I$H/stub -I$P -I$H"

$CXX $H/logic_test.cpp $P/app_logic.cpp -o "$O/logic"
"$O/logic"

echo
$CXX $H/breath_replay.cpp $H/stub_impl.cpp $P/app_logic.cpp $P/task_sense.cpp \
     $P/breath_filter.cpp $P/breath_slope.cpp -o "$O/replay"
"$O/replay" data/*.csv
