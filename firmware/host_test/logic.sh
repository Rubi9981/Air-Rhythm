#!/bin/sh
# 화면 상태 머신(app_logic) 단위 테스트 — 버튼 시나리오로 모터가 언제 돌고 서는지 검증한다.
#
#   sh firmware/host_test/logic.sh
#
# 저장소 최상위에서 실행할 것. app_logic.cpp 를 고치면 반드시 돌린다.
set -e
H=firmware/host_test
P=firmware/prototype
O=$(mktemp -d)
CXX="${CXX:-c++} -std=gnu++17 -O2 -Wall -Wextra -I$H/stub -I$P -I$H"

$CXX $H/logic_test.cpp $P/app_logic.cpp -o "$O/logic"
"$O/logic" || { rm -rf "$O"; exit 1; }
rm -rf "$O"
