#!/bin/sh
# 검출 결과 회귀 검증 — breath_slope.cpp / breath_filter.cpp 를 고칠 때 쓴다.
#
# run.sh 는 drv_old 와 drv_new 를 비교하는데, 둘 다 같은 breath_slope.cpp 를
# 링크한다. 그래서 그 파일 안에서 로직이 깨지면 양쪽이 똑같이 깨져 "일치"로 나온다.
# 이 스크립트는 지금 출력을 저장해 둔 골든과 비교해 그 구멍을 막는다.
#
# 골든은 '#' 로 시작하는 줄(이벤트·진단)만 담는다. 샘플 줄은 입력을 그대로 되받는
# 것이라 검출 로직이 바뀌어도 변할 수 없고, 파일만 커진다.
#
#   sh firmware/host_test/golden.sh            비교
#   sh firmware/host_test/golden.sh --update   골든 갱신 (의도한 변경일 때만)
#
# REPORT_SAMPLE 설정과 무관하게 동작한다.
set -e
H=firmware/host_test
P=firmware/prototype
G=$H/golden
O=$(mktemp -d)
CXX="${CXX:-c++} -std=gnu++17 -O2 -I$H/stub -I$P -I$H"

$CXX $H/drv_new.cpp $H/stub_impl.cpp $P/task_sense.cpp $P/link_msg.cpp \
     $P/breath_filter.cpp $P/breath_slope.cpp -o "$O/new"

update=0
[ "$1" = "--update" ] && update=1
fail=0
for f in data/*.csv; do
    name=$(basename "$f" .csv)
    "$O/new" "$f" | grep '^#' > "$O/cur.txt" || true
    if [ $update -eq 1 ]; then
        cp "$O/cur.txt" "$G/$name.txt"
        printf "  갱신   %-28s %s줄\n" "$name" "$(wc -l < "$G/$name.txt" | tr -d ' ')"
    elif [ ! -f "$G/$name.txt" ]; then
        printf "  골든없음 %s\n" "$name"; fail=1
    elif diff -q "$G/$name.txt" "$O/cur.txt" >/dev/null; then
        printf "  일치   %-28s %s줄\n" "$name" "$(wc -l < "$O/cur.txt" | tr -d ' ')"
    else
        printf "  불일치 %s\n" "$name"; diff "$G/$name.txt" "$O/cur.txt" | head -6; fail=1
    fi
done
rm -rf "$O"
[ $update -eq 1 ] && { echo "→ 골든 갱신 완료"; exit 0; }
[ $fail -eq 0 ] && echo "→ 검출 결과 동일" || { echo "→ 회귀 발생"; exit 1; }
