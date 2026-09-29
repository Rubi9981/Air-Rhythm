"""정확도 평가용 기준 라벨 초안 만들기 + 검수용 그림.

녹음 전체를 보고(비인과적) 흡기/호기 시작점을 찍어 data/labels/<이름>.csv 에 쓰고,
images/<이름>_labels.png 에 검수용 그림을 그린다. 그림에는 펌웨어 검출 결과도 함께
찍혀 있어, 라벨이 틀린 곳과 검출이 틀린 곳을 한눈에 가를 수 있다.

검수 방법:
  1. 그림에서 세로선(기준)이 파형의 골/마루에 제대로 붙어 있는지 본다
  2. 틀린 줄은 라벨 CSV 에서 고치거나 지운다. 판단이 안 되는 구간(기침·자세 변경)은
     그 시작 샘플에 'skip' 줄을 넣고, 끝난 곳에 다음 inhale/exhale 줄이 오게 한다
  3. 다 봤으면 첫 줄의 reviewed=no 를 reviewed=yes 로 바꾼다

이미 있는 라벨은 덮어쓰지 않는다(손으로 고친 것일 수 있으므로). 초안을 다시 만들려면
--force. 단, reviewed=yes 인 파일은 --force 로도 덮어쓰지 않는다 — 지우고 다시 만들 것.

실행:
    python scripts/label_reference.py                       평가 가능한 data/*.csv 전부
    python scripts/label_reference.py data/breath_...csv    하나만 (그림 창도 띄움)
    python scripts/label_reference.py --force               미검수 초안을 다시 만들기
"""

import argparse
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from breath import config
from breath import io_firmware
from breath.filters import bandpass_butter2, estimate_sample_rate
from breath.io_csv import read_csv, image_path_for
from breath.reference import (reference_labels, zero_phase_bandpass, label_path_for,
                              write_labels, read_labels)


def plot_review(path, times, mvs, labels, det, save, show):
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D
    from matplotlib.patches import Patch

    from plotting.plotting import use_korean_font

    use_korean_font()
    fs = estimate_sample_rate(times)
    y_ref = zero_phase_bandpass(mvs, fs)
    y_bp = bandpass_butter2(mvs, fs, config.HP_HZ, config.LP_HZ)

    width = max(14, min(60, (times[-1] - times[0]) / 5))    # 5초에 1인치 — 확대 없이 읽히게
    fig, ax = plt.subplots(figsize=(width, 5))
    ax.plot(times, y_ref, color="#333333", lw=1.2)
    ax.plot(times, y_bp, color=config.BUTTER_COLOR, lw=0.8, alpha=0.5)

    # 무호흡(none)·제외(skip) 구간 음영
    for (n, typ), nxt in zip(labels, labels[1:] + [(len(times) - 1, None)]):
        if typ in ("none", "skip"):
            ax.axvspan(times[n], times[nxt[0]], color=config.APNEA_COLOR,
                       alpha=0.30 if typ == "none" else 0.12, lw=0)
    for n, typ in labels:
        if typ in ("inhale", "exhale"):
            c = config.INHALE_COLOR if typ == "inhale" else config.EXHALE_COLOR
            ax.axvline(times[n], color=c, lw=0.9, alpha=0.8)

    # 펌웨어 검출: 확정 시점에 삼각형, 극점과 점선으로 잇는다
    lo, hi = min(y_ref), max(y_ref)
    for kind, color, marker, yy in (("inhale", config.INHALE_COLOR, "^", lo),
                                    ("exhale", config.EXHALE_COLOR, "v", hi)):
        for n in det[kind]:
            if n >= len(times):
                continue
            ax.plot(times[n], yy, marker=marker, color=color, ms=7, ls="None")
            e = det["ext"].get(n)
            if e is not None and e < len(times):
                ax.plot([times[e], times[n]], [yy, yy], color=color, lw=0.8, ls=":")

    handles = [Line2D([0], [0], color="#333333", lw=1.2, label="zero-phase BP (reference)"),
               Line2D([0], [0], color=config.BUTTER_COLOR, lw=0.8, alpha=0.5, label="causal BP (device)"),
               Line2D([0], [0], color=config.INHALE_COLOR, lw=0.9, label="ref inhale"),
               Line2D([0], [0], color=config.EXHALE_COLOR, lw=0.9, label="ref exhale"),
               Line2D([0], [0], color=config.INHALE_COLOR, marker="^", ls="None", label="fw inhale"),
               Line2D([0], [0], color=config.EXHALE_COLOR, marker="v", ls="None", label="fw exhale"),
               Patch(facecolor=config.APNEA_COLOR, alpha=0.30, label="none"),
               Patch(facecolor=config.APNEA_COLOR, alpha=0.12, label="skip")]
    ax.legend(handles=handles, loc="upper right", ncol=4, fontsize=8, framealpha=0.9)
    ax.set_xlim(times[0], times[-1])
    ax.set_xlabel("time (s)")
    ax.set_ylabel("band-pass (mV)")
    ax.grid(True, alpha=0.25)
    ax.set_title(f"{os.path.basename(path)}  |  reference labels vs firmware")
    fig.tight_layout()
    os.makedirs(os.path.dirname(save) or ".", exist_ok=True)
    fig.savefig(save, dpi=110)
    print(f"    그림 {save}")
    if show:
        plt.show()
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description="기준 라벨 초안 + 검수 그림")
    ap.add_argument("csv", nargs="*", help="CSV 파일 (생략하면 data/*.csv)")
    ap.add_argument("--force", action="store_true", help="미검수 라벨을 다시 만든다")
    ap.add_argument("--no-plot", action="store_true", help="그림을 그리지 않는다")
    args = ap.parse_args()

    paths = args.csv or sorted(glob.glob(os.path.join("data", "*.csv")))
    exe = None if args.no_plot else io_firmware.build()
    for path in paths:
        name = os.path.basename(path)
        times, _raws, mvs, _phases = read_csv(path)
        why = io_firmware.unfit_reason(path, times)
        if why:
            print(f"  건너뜀 {name}: {why}")
            continue

        lpath = label_path_for(path)
        if os.path.exists(lpath):
            _, reviewed = read_labels(lpath)
            if reviewed or not args.force:
                state = "검수 완료" if reviewed else "초안"
                print(f"  유지   {name}: {lpath} ({state})")
            else:
                write_labels(lpath, reference_labels(mvs, estimate_sample_rate(times)), times)
                print(f"  갱신   {name}: {lpath}")
        else:
            write_labels(lpath, reference_labels(mvs, estimate_sample_rate(times)), times)
            print(f"  생성   {name}: {lpath}")

        if exe:
            labels, _ = read_labels(lpath)
            det = io_firmware.replay(exe, path)
            plot_review(path, times, mvs, labels, det,
                        image_path_for(path, "_labels.png"), show=len(paths) == 1)


if __name__ == "__main__":
    main()
