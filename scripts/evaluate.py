"""호흡 검출 정확도 평가 — 펌웨어 C 코드를 호스트에서 빌드해 기준 라벨로 채점한다.

기존 host_test 는 "출력이 예전과 같은가"(golden)와 "안전 규칙을 지키는가"(breath_replay)를
본다. 이 스크립트는 "실제 호흡과 맞는가"를 숫자로 낸다. 알고리즘을 바꾸면 golden 은
당연히 깨지므로, 바꾼 것이 개선인지를 여기서 판단한 뒤 golden.sh --update 한다.

채점 대상은 파이썬 검출기가 아니라 firmware/prototype/ 의 C 소스다(breath/io_firmware.py).
기준 라벨은 scripts/label_reference.py 로 만들고 사람이 검수한다. 지표의 정의는
breath/metrics.py 맨 위에 있다.

실행:
    python scripts/evaluate.py                               현재 설정의 성적표
    python scripts/evaluate.py -D K_SLOPE=0.3                값 하나 바꿔서
    python scripts/evaluate.py --sweep K_SLOPE=0.2,0.25,0.3  값 여러 개 비교(여러 번 주면 조합)
    python scripts/evaluate.py --save-baseline B.json        현재 성적을 기준선으로 저장
    python scripts/evaluate.py --check B.json                기준선보다 나빠졌으면 종료코드 1
"""

import argparse
import glob
import itertools
import json
import os
import sys
import unicodedata

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from breath import io_firmware, metrics
from breath.io_csv import read_csv
from breath.reference import label_path_for, read_labels


# ============================================================
#  [설정]
# ============================================================
EVAL_START_S = 14.0     # 이 시각 전은 채점하지 않는다. 정착(SETTLE_S=12s)+여유.
                        # SETTLE_S 를 바꿔 비교할 때도 구간이 같도록 고정값으로 둔다
EVAL_TAIL_S = 2.0       # 녹음 끝 이만큼은 채점하지 않는다(마지막 극점은 확정될 수 없음)

# --check 의 허용 폭 — 기준선 대비 이보다 나빠지면 회귀
CHECK_TOL = {
    "exhale.f1": -0.01,              # 낮아지면 나쁨 (음수 = 허용 하락폭)
    "inhale.f1": -0.01,
    "exhale_coverage": -0.02,
    "hit_in_inhale": +0.005,         # 높아지면 나쁨 (양수 = 허용 상승폭)
    "hit_in_none": +0.005,
    "exhale.latency_median_s": +0.02,
    "exhale.latency_p90_s": +0.04,
}
# ============================================================


def find_recordings(paths):
    """채점할 (csv, 라벨) 목록과 건너뛴 이유 목록."""
    use, skipped = [], []
    for path in paths or sorted(glob.glob(os.path.join("data", "*.csv"))):
        name = os.path.basename(path)
        why = io_firmware.unfit_reason(path)
        if why:
            skipped.append((name, why))
            continue
        lpath = label_path_for(path)
        if not os.path.exists(lpath):
            skipped.append((name, "라벨 없음 — scripts/label_reference.py"))
            continue
        use.append((path, lpath))
    return use, skipped


def run_config(recordings, defines):
    """한 설정으로 모든 녹음을 채점. 반환: ({이름: raw}, {이름: 검수여부}, 컴파일된 설정)."""
    exe = io_firmware.build(defines)
    fs = io_firmware.FS_HZ
    raws, reviewed, compiled = {}, {}, None
    for path, lpath in recordings:
        det = io_firmware.replay(exe, path)
        compiled = det["config"]
        labels, reviewed[os.path.basename(path)] = read_labels(lpath)
        start = int(EVAL_START_S * fs)
        end = det["n_total"] - int(EVAL_TAIL_S * fs)
        if end <= start:
            continue
        raws[os.path.basename(path)] = metrics.evaluate(labels, det, det["n_total"], fs, start, end)
    return raws, reviewed, compiled


# --- 출력 ---
def _pct(v):
    return "   -" if v is None else f"{v * 100:4.0f}"


def _ms(v):
    return "   -" if v is None else f"{v * 1000:4.0f}"


def _f(v, w=5, p=1):
    return " " * (w - 1) + "-" if v is None else f"{v:{w}.{p}f}"


def _pad(text, width):
    """한글은 터미널에서 두 칸을 차지하므로 글자 수가 아니라 표시 폭으로 채운다."""
    shown = sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in text)
    return text + " " * max(0, width - shown)


HEADER = ("                              ───── 호기 onset ─────────────  ── 흡기 onset ──  ── 타진(샘플) ──   bpm\n"
          "  녹음                        Se%  PPV%  FP  지연ms 중앙/p90/최대  Se%  PPV%  FP  흡기%  호기커버%  오차")


def row(label, s, width=26):
    e, i = s["exhale"], s["inhale"]
    return (f"  {_pad(label, width)} {_pct(e['sensitivity'])} {_pct(e['ppv'])} {e['fp']:3d}  "
            f"{_ms(e['latency_median_s'])} / {_ms(e['latency_p90_s'])} / {_ms(e['latency_max_s'])}  "
            f"{_pct(i['sensitivity'])} {_pct(i['ppv'])} {i['fp']:3d}  "
            f"{_f(None if s['hit_in_inhale'] is None else s['hit_in_inhale'] * 100, 5)}  "
            f"{_pct(s['exhale_coverage'])}      {_f(s['bpm_mae'])}")


def print_report(raws, reviewed):
    print(HEADER)
    for name, raw in raws.items():
        mark = "" if reviewed[name] else " *"
        print(row(os.path.splitext(name)[0][:24] + mark, metrics.summarize(raw)))
    print(row("합계", metrics.summarize(metrics.merge(list(raws.values())))))
    if not all(reviewed.values()):
        print("  * 라벨 미검수 — 자동 초안 기준이라 숫자를 그대로 믿지 말 것")


def _get(summary, key):
    v = summary
    for k in key.split("."):
        v = v[k]
    return v


def check(raws, baseline_path):
    """기준선과 같은 녹음끼리만 합쳐 비교한다(개인 녹음은 사람마다 있을 수도 없을 수도)."""
    with open(baseline_path, encoding="utf-8") as f:
        base = json.load(f)
    common = sorted(set(raws) & set(base["files"]))
    if not common:
        print("기준선과 겹치는 녹음이 없습니다.")
        return False
    cur = metrics.summarize(metrics.merge([raws[n] for n in common]))
    old = metrics.summarize(metrics.merge([base["files"][n] for n in common]))
    print(f"\n기준선 비교 ({baseline_path}, 녹음 {len(common)}개)")
    ok = True
    for key, tol in CHECK_TOL.items():
        a, b = _get(old, key), _get(cur, key)
        if a is None or b is None:
            continue
        worse = (b - a < tol) if tol < 0 else (b - a > tol)
        ok &= not worse
        print(f"  {'회귀' if worse else '통과'}  {key:<26} {a:9.4f} → {b:9.4f}")
    return ok


def parse_defines(items):
    out = {}
    for it in items or []:
        k, _, v = it.partition("=")
        out[k.strip()] = v.strip()
    return out


def main():
    ap = argparse.ArgumentParser(description="펌웨어 호흡 검출 정확도 평가")
    ap.add_argument("csv", nargs="*", help="CSV 파일 (생략하면 data/*.csv 중 라벨 있는 것)")
    ap.add_argument("-D", dest="define", action="append", metavar="NAME=VAL",
                    help="breath_config.h 조정값 덮어쓰기")
    ap.add_argument("--sweep", action="append", metavar="NAME=V1,V2,...",
                    help="값 여러 개를 합계로만 비교")
    ap.add_argument("--save-baseline", metavar="JSON")
    ap.add_argument("--check", metavar="JSON")
    args = ap.parse_args()

    recordings, skipped = find_recordings(args.csv)
    for name, why in skipped:
        print(f"  건너뜀 {name}: {why}")
    if not recordings:
        sys.exit("채점할 녹음이 없습니다.")
    base = parse_defines(args.define)

    if args.sweep:
        axes = []
        for it in args.sweep:
            k, _, vs = it.partition("=")
            axes.append([(k.strip(), v.strip()) for v in vs.split(",")])
        combos = list(itertools.product(*axes))
        labels = [" ".join(f"{k}={v}" for k, v in c) for c in combos]
        width = max(26, max(len(x) for x in labels))
        print(f"\n녹음 {len(recordings)}개, 합계 성적")
        head = HEADER.replace("  녹음    ", "  설정    ").splitlines()
        print("\n".join(h[:28] + " " * (width - 26) + h[28:] for h in head))
        for combo, label in zip(combos, labels):
            raws, _, _ = run_config(recordings, dict(base, **dict(combo)))
            print(row(label, metrics.summarize(metrics.merge(list(raws.values()))), width))
        return

    raws, reviewed, compiled = run_config(recordings, base)
    changed = ", ".join(f"{k}={v}" for k, v in base.items()) or "기본값"
    print(f"\n설정: {changed}")
    print_report(raws, reviewed)

    if args.save_baseline:
        with open(args.save_baseline, "w", encoding="utf-8") as f:
            json.dump({"config": compiled, "files": raws}, f, ensure_ascii=False, indent=1)
        print(f"\n기준선 저장: {args.save_baseline}")
    if args.check and not check(raws, args.check):
        print("→ 기준선보다 나빠짐")
        sys.exit(1)
    elif args.check:
        print("→ 기준선 이상")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError) as e:     # 조정값 이름 오류·빌드 실패
        sys.exit(str(e))
