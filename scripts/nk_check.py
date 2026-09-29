"""neurokit2 기준 vs 펌웨어 검출 — 흡기/호기 진입 횟수와 진입 시각 비교.

녹음 CSV 의 원본 mV 를 neurokit2(khodadad2018)에 넣어 골(흡기 진입)·마루(호기 진입)를
찾고, 같은 CSV 를 펌웨어 C 코드(breath/io_firmware.py)로 재생해 나온 흡기/호기 확정
시각과 맞대어 본다.

neurokit2 에는 기기의 필터 출력이 아니라 원본 mV 를 넣는다. 필터 출력을 넣으면 기준이
인과 필터의 지연(약 200ms)을 안고 들어가 펌웨어가 실제보다 빨라 보인다.
neurokit2 는 녹음 전체를 보고(양방향 필터) 판정하므로 기준으로는 쓸 수 있지만
MCU 로 옮길 수는 없다.

기준이 늘 옳은 것은 아니다. 파형에 어깨·이중 봉우리가 많은 녹음에서는 neurokit2 가 그것까지
호흡으로 세어 "놓침" 이 부풀려진다(NK_AMPLITUDE_MIN). 횟수 차이가 크면 파형을 직접 볼 것.

짝짓기: 기준 진입 시각보다 -0.3초 ~ +1.0초 안에 펌웨어가 같은 종류를 확정하면 한 쌍.
  짝 없는 기준 = 놓침, 짝 없는 펌웨어 = 오검출. 지연 = 펌웨어 확정 - 기준 진입.

실행 (neurokit2 필요: pip install neurokit2):
    python scripts/nk_check.py                         data/*.csv 전부, 녹음별 요약
    python scripts/nk_check.py data/breath_...csv -v   진입 시각을 하나씩
    python scripts/nk_check.py data/breath_...csv --plot   그래프 (images/<이름>_nk.png, 창도 띄움)
    python scripts/nk_check.py data/S01_...csv -v --plot  키보드 라벨이 있는 녹음 (record_labeled.py)

그래프 (--plot):
  위    neurokit2 가 정리한 파형(RSP_Clean)과 기기 필터 파형 — firmware/prototype/breath_filter.cpp
        를 호스트에서 빌드해 float 로 돌린 출력 그대로다(파이썬 이식본이 아니다)
  아래  같은 두 파형 위에 neurokit2 의 흡기/호기 진입(○, 골/마루)과 펌웨어의 흡기/호기
        확정(▲▼)을 겹친다. 짝지어진 것은 점선으로 이어 지연이 보이게 한다. 점선 없이 혼자
        있는 ○ 는 놓침, ▲▼ 는 오검출이다. 회색 구간은 세지 않는 구간이다.

키보드 라벨 (scripts/record_labeled.py 로 기록한 CSV 의 key 열):
  있으면 세 번째 기준으로 쓴다. 키 진입마다 ±KEY_TOL_S 안에서 펌웨어·neurokit2 를 따로
  짝지어 "키는 있는데 없음(놓침)" 과 "키에 없는 것(초과)" 을 센다. 사람의 반응 지연이 섞이므로
  지연은 내지 않는다. 그래프에는 세로 점선으로 그린다.
    키 ≈ neurokit2 인데 펌웨어만 다르다  → 검출 알고리즘 문제
    키 ≠ neurokit2                        → 센서 신호 문제이거나 neurokit2 가 어깨를 셌다
"""

import argparse
import glob
import os
import sys
import warnings
from statistics import median

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from breath import config, io_firmware
from breath.io_csv import read_csv, read_key_onsets, image_path_for
from breath.metrics import match_events


# ============================================================
#  [설정]
# ============================================================
NK_METHOD = "khodadad2018"   # neurokit2 정리·극점 검출 방법
NK_AMPLITUDE_MIN = 0.3       # 극점 인정 높이 = 이웃 극점 높이차 중앙값 × 이 비율 (neurokit2 기본 0.3).
                             # 낮으면 어깨·이중 봉우리까지 호흡으로 센다. 기준 횟수가 이 값에 민감하다
START_S = 14.0               # 이 시각 전은 세지 않는다. 기기 정착(12s) + 여유
TAIL_S = 2.0                 # 녹음 끝 이만큼은 세지 않는다(마지막 극점은 확정될 수 없음)
KEY_TOL_S = 0.7              # 키보드 라벨과 짝지을 때 앞뒤 허용 폭(초). 반응 지연 200~400ms 를 덮는다
# ============================================================

FS = io_firmware.FS_HZ
KINDS = (("inhale", "흡기"), ("exhale", "호기"))


def nk_process(mvs):
    """원본 mV → neurokit2 의 (정리된 파형, 흡기 진입 n 목록, 호기 진입 n 목록)."""
    try:
        import numpy as np
        import neurokit2 as nk
    except ImportError:
        sys.exit("neurokit2 가 필요합니다.  pip install neurokit2 로 설치하세요.")
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        clean = nk.rsp_clean(np.asarray(mvs, dtype=float), sampling_rate=FS, method=NK_METHOD)
        _, info = nk.rsp_peaks(clean, sampling_rate=FS, method=NK_METHOD,
                               amplitude_min=NK_AMPLITUDE_MIN)
    # 골 = 흡기 진입, 마루 = 호기 진입 (POLARITY=+1: 상승=흡기)
    return (list(clean), [int(n) for n in info["RSP_Troughs"]],
            [int(n) for n in info["RSP_Peaks"]])


def compare(path, exe, with_filt=False):
    """한 녹음을 비교한다.

    반환: (res, ctx)
      res  {kind: (기준 n, 펌웨어 n, 짝, 놓침, 오검출)}. 너무 짧으면 None
      ctx  그래프용 {times, mvs, clean, ref, det, lo, hi}
    """
    times, _raws, mvs, _phases = read_csv(path)
    lo, hi = int(START_S * FS), len(times) - int(TAIL_S * FS)
    ctx = {"times": times, "mvs": mvs, "lo": lo, "hi": hi}
    if hi <= lo:
        return None, ctx

    clean, inh, exh = nk_process(mvs)
    ref = {"inhale": inh, "exhale": exh}
    det = io_firmware.replay(exe, path, with_filt=with_filt)
    ctx.update(clean=clean, ref=ref, det=det, key=read_key_onsets(path), key_res=None)
    out = {}
    for kind, _ in KINDS:
        r = [n for n in ref[kind] if lo <= n < hi]
        d = [n for n in det[kind] if lo <= n < hi]
        pairs, missed, extra = match_events(r, d, FS)
        out[kind] = (r, d, pairs, missed, extra)

    if ctx["key"]:
        ctx["key_res"] = {}
        for kind, _ in KINDS:
            k = [n for n in ctx["key"][kind] if lo <= n < hi]
            one = {"key": k}
            for src, ns in (("fw", det[kind]), ("nk", ref[kind])):
                ns = [n for n in ns if lo <= n < hi]
                one[src] = match_events(k, ns, FS, tol_early_s=KEY_TOL_S, tol_late_s=KEY_TOL_S)
            ctx["key_res"][kind] = one
    return out, ctx


def fmt_delay(pairs):
    if not pairs:
        return "      -"
    return f"{median((d - r) / FS for r, d in pairs) * 1000:5.0f}ms"


def print_summary(name, res):
    parts = []
    for kind, ko in KINDS:
        r, d, pairs, missed, extra = res[kind]
        parts.append(f"{ko} 기준 {len(r):3d} / 펌웨어 {len(d):3d}  짝 {len(pairs):3d}  "
                     f"놓침 {len(missed):2d}  오검출 {len(extra):2d}  지연 중앙 {fmt_delay(pairs)}")
    print(f"  {name}")
    for p in parts:
        print(f"      {p}")


def print_key_summary(key_res):
    """키보드 라벨 대비 펌웨어·neurokit2."""
    for kind, ko in KINDS:
        one = key_res[kind]
        parts = []
        for src, label in (("fw", "펌웨어"), ("nk", "neurokit2")):
            pairs, missed, extra = one[src]
            parts.append(f"{label} 짝 {len(pairs):3d} 놓침 {len(missed):2d} 초과 {len(extra):2d}")
        print(f"      키 {ko} {len(one['key']):3d}회 →  " + "   ".join(parts))


def print_timeline(res, times):
    """진입을 시간순으로 한 줄씩."""
    rows = []
    for kind, ko in KINDS:
        _r, _d, pairs, missed, extra = res[kind]
        rows += [(r, ko, r, d) for r, d in pairs]
        rows += [(r, ko, r, None) for r in missed]
        rows += [(d, ko, None, d) for d in extra]
    rows.sort()
    print("      시각(s)  종류   기준(s)  펌웨어(s)   차이")
    for _, ko, r, d in rows:
        rs = f"{times[r]:8.2f}" if r is not None else "       -"
        ds = f"{times[d]:9.2f}" if d is not None else "        -"
        if r is not None and d is not None:
            diff = f"{(d - r) / FS * 1000:+5.0f}ms"
        else:
            diff = "  놓침" if d is None else "  오검출"
        at = times[r if r is not None else d]
        print(f"      {at:7.2f}  {ko}  {rs}  {ds}   {diff}")


def plot_compare(name, res, ctx, save, show):
    """위: 두 파형 겹침. 아래: 두 파형 + neurokit2 진입(○)·펌웨어 확정(▲▼)."""
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D
    from matplotlib.patches import Patch

    from plotting.plotting import use_korean_font

    use_korean_font()
    times, clean = ctx["times"], ctx["clean"]
    y_dev = ctx["det"]["filt"]                                 # breath_filter.cpp 출력(float)
    cfg = ctx["det"]["config"]                                 # 실제로 컴파일된 값
    band = f"{cfg['HP_HZ']:g}-{cfg['LP_HZ']:g}Hz"
    nk_color, dev_color = "#1f77b4", "#333333"

    width = max(14, min(60, (times[-1] - times[0]) / 5))      # 5초에 1인치 — 확대 없이 읽히게
    fig, (ax_w, ax_d) = plt.subplots(2, 1, sharex=True, figsize=(width, 8))

    # --- 위: 파형 비교 ---
    ax_w.plot(times, clean, color=nk_color, lw=1.2)
    ax_w.plot(times, y_dev, color=dev_color, lw=1.0)
    ax_w.axhline(0.0, color="#999999", lw=0.7)
    ax_w.set_ylabel("mV")
    ax_w.set_title(f"{name}  |  파형: neurokit2 {NK_METHOD} vs 기기 필터 "
                   f"({band})")
    ax_w.legend(handles=[
        Line2D([0], [0], color=nk_color, lw=1.2, label=f"neurokit2 RSP_Clean ({NK_METHOD}, 양방향)"),
        Line2D([0], [0], color=dev_color, lw=1.0, label=f"기기 필터 breath_filter.cpp ({band}, 인과)"),
    ], loc="upper right", fontsize=8, framealpha=0.9)

    # --- 아래: 검출 비교 ---
    ax_d.plot(times, clean, color=nk_color, lw=0.9, alpha=0.45)
    ax_d.plot(times, y_dev, color=dev_color, lw=0.9, alpha=0.45)
    colors = {"inhale": config.INHALE_COLOR, "exhale": config.EXHALE_COLOR}
    for kind, _ko in KINDS:
        c = colors[kind]
        _r, _d, pairs, _missed, _extra = res[kind]
        for r, d in pairs:                                     # 짝: 기준 극점 → 펌웨어 확정
            ax_d.plot([times[r], times[d]], [clean[r], y_dev[d]], color=c, lw=0.8, ls=":")
        ref_ns = [n for n in ctx["ref"][kind] if n < len(times)]
        ax_d.plot([times[n] for n in ref_ns], [clean[n] for n in ref_ns], ls="None",
                  marker="o", ms=7, mfc="none", mec=c, mew=1.5)
        det_ns = [n for n in ctx["det"][kind] if n < len(times)]
        ax_d.plot([times[n] for n in det_ns], [y_dev[n] for n in det_ns], ls="None",
                  marker="^" if kind == "inhale" else "v", ms=8, color=c)
    if ctx["key"]:
        for kind, _ko in KINDS:
            for n in ctx["key"][kind]:
                if n < len(times):
                    ax_d.axvline(times[n], color=colors[kind], lw=1.0, ls="--", alpha=0.6)
    ax_d.set_ylabel("mV")
    ax_d.set_xlabel("time (s)")
    summary = "   ".join(
        f"{ko} 기준 {len(res[k][0])} / 펌웨어 {len(res[k][1])} (놓침 {len(res[k][3])}, "
        f"오검출 {len(res[k][4])})" for k, ko in KINDS)
    keys = " vs 키보드 점선" if ctx["key"] else ""
    ax_d.set_title(f"검출: neurokit2 ○ vs 펌웨어 ▲▼{keys}   |   {summary}", fontsize=10)
    ax_d.legend(handles=[
        Line2D([0], [0], color=config.INHALE_COLOR, marker="o", mfc="none", ls="None", label="neurokit2 흡기 진입(골)"),
        Line2D([0], [0], color=config.EXHALE_COLOR, marker="o", mfc="none", ls="None", label="neurokit2 호기 진입(마루)"),
        Line2D([0], [0], color=config.INHALE_COLOR, marker="^", ls="None", label="펌웨어 흡기 확정"),
        Line2D([0], [0], color=config.EXHALE_COLOR, marker="v", ls="None", label="펌웨어 호기 확정"),
        Line2D([0], [0], color="#666666", ls=":", label="짝 (지연)"),
        Patch(facecolor=config.APNEA_COLOR, alpha=0.2, label="세지 않는 구간"),
    ] + ([
        Line2D([0], [0], color=config.INHALE_COLOR, ls="--", label="키 i (흡기)"),
        Line2D([0], [0], color=config.EXHALE_COLOR, ls="--", label="키 e (호기)"),
    ] if ctx["key"] else []), loc="upper right", ncol=4, fontsize=8, framealpha=0.9)

    for ax in (ax_w, ax_d):
        ax.axvspan(times[0], times[ctx["lo"]], color=config.APNEA_COLOR, alpha=0.2, lw=0)
        ax.axvspan(times[ctx["hi"]], times[-1], color=config.APNEA_COLOR, alpha=0.2, lw=0)
        ax.grid(True, alpha=0.25)
    ax_w.set_xlim(times[0], times[-1])
    fig.tight_layout()
    os.makedirs(os.path.dirname(save) or ".", exist_ok=True)
    fig.savefig(save, dpi=110)
    print(f"      그림 {save}")
    if show:
        plt.show()
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description="neurokit2 기준 vs 펌웨어 — 진입 횟수·시각")
    ap.add_argument("csv", nargs="*", help="CSV 파일 (생략하면 data/*.csv)")
    ap.add_argument("-v", "--verbose", action="store_true", help="진입 시각을 하나씩 출력")
    ap.add_argument("--plot", action="store_true", help="파형·검출 비교 그래프 저장 (파일 하나면 창도 띄움)")
    args = ap.parse_args()
    paths = args.csv or sorted(glob.glob(os.path.join("data", "*.csv")))

    exe = io_firmware.build()
    total = {kind: [0, 0, 0, 0, 0, []] for kind, _ in KINDS}   # 기준·펌웨어·짝·놓침·오검출·짝목록
    key_total = {kind: {"key": [], "fw": ([], [], []), "nk": ([], [], [])} for kind, _ in KINDS}
    any_key = False
    print(f"기준: neurokit2 {NK_METHOD} amplitude_min={NK_AMPLITUDE_MIN} (원본 mV)   "
          f"구간: {START_S:.0f}초 ~ 끝-{TAIL_S:.0f}초")
    for path in paths:
        name = os.path.basename(path)
        why = io_firmware.unfit_reason(path)
        if why:
            print(f"  건너뜀 {name}: {why}")
            continue
        res, ctx = compare(path, exe, with_filt=args.plot)
        if res is None:
            print(f"  건너뜀 {name}: 너무 짧음")
            continue
        print_summary(name, res)
        if ctx["key_res"]:
            any_key = True
            print_key_summary(ctx["key_res"])
            for kind, _ in KINDS:
                one, tot = ctx["key_res"][kind], key_total[kind]
                tot["key"] += one["key"]
                for src in ("fw", "nk"):
                    for a, b in zip(tot[src], one[src]):
                        a += b
        if args.verbose:
            print_timeline(res, ctx["times"])
        if args.plot:
            plot_compare(name, res, ctx, image_path_for(path, "_nk.png"), show=len(paths) == 1)
        for kind, _ in KINDS:
            r, d, pairs, missed, extra = res[kind]
            t = total[kind]
            t[0] += len(r); t[1] += len(d); t[2] += len(pairs)
            t[3] += len(missed); t[4] += len(extra); t[5] += pairs

    print("\n  합계")
    for kind, ko in KINDS:
        r, d, p, m, e, pairs = total[kind]
        print(f"      {ko} 기준 {r:3d} / 펌웨어 {d:3d}  짝 {p:3d}  놓침 {m:2d}  오검출 {e:2d}  "
              f"지연 중앙 {fmt_delay(pairs)}")
    if any_key:
        print_key_summary(key_total)


if __name__ == "__main__":
    main()
