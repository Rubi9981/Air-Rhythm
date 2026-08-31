#!/bin/sh
# 호스트 대조 검증 — 녹음 CSV 를 재생해 펌웨어의 출력이 바뀌지 않았는지 확인한다.
#
# Arduino/FreeRTOS 를 stub/ 로 대신하고, firmware/prototype/ 의 실제 소스를 그대로
# 링크한다. 따라서 task_sense.cpp / link_msg.cpp 를 고치면 여기서 바로 드러난다.
#
#   drv_old : breath_monitor.ino 의 단일 loop() 구조를 재현 (0단계 기준선)
#   drv_new : firmware/prototype/ 의 sense_step + msg_report 를 그대로 구동
#
# 사용법:  sh firmware/host_test/run.sh
# 저장소 최상위에서 실행할 것. 모든 CSV 에서 두 출력이 같으면 통과.
#
# 0단계가 끝나면 drv_old.cpp 는 지워도 된다(그때부터는 drv_new 의 출력을 직전
# 커밋과 비교하는 방식이 더 유용하다).
set -e
H=firmware/host_test
P=firmware/prototype
O=$(mktemp -d)
CXX="${CXX:-c++} -std=gnu++17 -O2 -I$H/stub -I$P -I$H"

$CXX $H/drv_old.cpp $H/stub_impl.cpp $P/breath_filter.cpp $P/breath_slope.cpp -o "$O/old"
$CXX $H/drv_new.cpp $H/stub_impl.cpp $P/task_sense.cpp $P/link_msg.cpp \
     $P/breath_filter.cpp $P/breath_slope.cpp -o "$O/new"

fail=0
for f in data/*.csv; do
    "$O/old" "$f" > "$O/o.txt"
    "$O/new" "$f" > "$O/n.txt"
    if diff -q "$O/o.txt" "$O/n.txt" >/dev/null; then
        printf "  일치   %s\n" "$(basename "$f")"
    else
        printf "  불일치 %s\n" "$(basename "$f")"; diff "$O/o.txt" "$O/n.txt" | head -8; fail=1
    fi
done
rm -rf "$O"
[ $fail = 0 ] && echo "→ 출력 동일" || { echo "→ 차이 발견"; exit 1; }
