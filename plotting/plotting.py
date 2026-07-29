"""matplotlib 그리기 — 신호 표현만 담당(검출 로직 없음).

  - plot_signals        : raw/mV 원본·스무딩·대역통과 다단 비교
  - plot_filter_compare : mV 에 1-pole vs 2차 Butterworth 대역통과 비교
  - plot_detection      : 대역통과 신호 위에 흡기/호기 onset 화살표(검출 결과는 인자로 받음)

읽기·필터·구간 계산은 breath 라이브러리를 가져다 쓴다.
"""

import os

from breath import config
from breath.io_csv import read_csv, target_spans, image_path_for
from breath.filters import (
    moving_average_causal, ema, resolve_alpha,
    bandpass_1pole, bandpass_butter2, estimate_sample_rate,
)


# -----------------------------------------------------------------------------
# 공용 그리기 헬퍼
# -----------------------------------------------------------------------------
def use_korean_font():
    """제목·라벨의 한글이 깨지지 않도록 설치된 한글 폰트를 고른다.

    파일 이름이 그래프 제목에 들어가는데(예: 최현수.csv), matplotlib 기본 폰트인
    DejaVu Sans 에는 한글 글리프가 없어 □□□ 로 나온다. 쓸 수 있는 폰트가 하나도
    없으면 조용히 넘어가고 기존 동작을 유지한다.

    matplotlib 을 import 하므로 그리기 직전에 호출한다(모듈 import 시점 아님).
    """
    from matplotlib import font_manager, rcParams

    installed = {f.name for f in font_manager.fontManager.ttflist}
    for name in ("AppleGothic", "Apple SD Gothic Neo", "NanumGothic", "Nanum Gothic",
                 "Malgun Gothic", "Noto Sans CJK KR", "Noto Sans KR"):
        if name in installed:
            rcParams["font.family"] = name
            rcParams["axes.unicode_minus"] = False   # 한글 폰트엔 U+2212 가 없는 경우가 많다
            return name
    return None


def shade_axis(ax, spans):
    """한 축에 흡기(빨강)/호기(초록) 배경을 칠한다."""
    for start, end, phase in spans:
        ax.axvspan(start, end,
                   color=config.INHALE_COLOR if phase == "inhale" else config.EXHALE_COLOR,
                   alpha=config.SPAN_ALPHA, linewidth=0, zorder=0)


def _draw_raw(ax, times, values, ylabel, color="#333333"):
    ax.plot(times, values, linewidth=0.9, color=color, label=ylabel, zorder=2)
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.25, zorder=1)


def _draw_smoothed(ax, times, values, smooth, alpha, ylabel):
    if smooth >= 2:
        ax.plot(times, moving_average_causal(values, smooth), linewidth=1.6,
                color="#1f77b4", label=f"Causal SMA ({smooth})", zorder=3)
    ax.plot(times, ema(values, alpha), linewidth=1.6,
            color="#ff7f0e", label=f"EMA (a={alpha:.2f})", zorder=3)
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.25, zorder=1)


def _draw_bandpass(ax, times, values, hp, lp, ylabel):
    ax.axhline(0.0, color="#999999", linewidth=0.8, zorder=1)
    ax.plot(times, bandpass_1pole(times, values, hp, lp), linewidth=1.6,
            color=config.ONEPOLE_COLOR, label=f"1-pole BP ({hp}-{lp}Hz)", zorder=3)
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.25, zorder=1)


def _draw_arrows(ax, onsets, color, direction, amp_ref):
    """onset 에 화살표. direction +1=위쪽(흡기), -1=아래쪽(호기)."""
    off = 0.55 * amp_ref * direction
    head = 0.30 * amp_ref * direction
    for t, y in onsets:
        ax.annotate("", xy=(t, y + head), xytext=(t, y + off),
                    arrowprops=dict(arrowstyle="-|>", color=color, lw=1.8,
                                    mutation_scale=14), zorder=6)


# -----------------------------------------------------------------------------
# 1) raw/mV 원본·스무딩·대역통과 다단 비교
# -----------------------------------------------------------------------------
def plot_signals(path, save=None, smooth=7, ema_alpha=None, xlim=None,
                 full_scale=False, shade=True, bandpass=True,
                 hp=config.HP_HZ, lp=config.LP_HZ,
                 inhale_s=config.INHALE_S, exhale_s=config.EXHALE_S, image_dpi=150):
    """raw·mV 각각의 원본/스무딩(/대역통과)을 세로로 나눠 그린다."""
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch

    use_korean_font()   # 파일 이름이 제목에 들어가므로 한글이 깨지지 않게

    if save is None:
        save = image_path_for(path, "_signals.png")
    alpha = resolve_alpha(smooth, ema_alpha)
    times, raws, mvs, phases = read_csv(path)
    raw_lo, raw_hi = min(raws), max(raws)
    mv_lo, mv_hi = min(mvs), max(mvs)

    panels = [("raw", raws, "ADC raw (0-4095)"), ("smooth", raws, "ADC raw (smoothed)")]
    if bandpass:
        panels.append(("bp", raws, "ADC raw (band-pass)"))
    panels += [("raw", mvs, "calibrated mV"), ("smooth", mvs, "mV (smoothed)")]
    if bandpass:
        panels.append(("bp", mvs, "mV (band-pass)"))

    n = len(panels)
    fig, axes = plt.subplots(n, 1, sharex=True, figsize=(13, 2.4 * n))
    if n == 1:
        axes = [axes]

    spans = target_spans(times, phases, inhale_s, exhale_s) if shade else []

    prev_raw_ax = None
    raw_smooth_pairs = []
    for ax, (kind, sig, ylab) in zip(axes, panels):
        if spans:
            shade_axis(ax, spans)
        if kind == "raw":
            _draw_raw(ax, times, sig, ylab)
            prev_raw_ax = ax
        elif kind == "smooth":
            _draw_smoothed(ax, times, sig, smooth, alpha, ylab)
            if prev_raw_ax is not None:
                raw_smooth_pairs.append((prev_raw_ax, ax))
        else:
            _draw_bandpass(ax, times, sig, hp, lp, ylab)

    axes[-1].set_xlabel("time (s)")
    axes[0].set_title(f"{os.path.basename(path)}   |  raw span {raw_hi - raw_lo:g}"
                      f"  /  mv span {mv_hi - mv_lo:g}")

    handles, _ = axes[0].get_legend_handles_labels()
    if spans:
        handles += [Patch(facecolor=config.INHALE_COLOR, alpha=config.SPAN_ALPHA + 0.15, label="inhale"),
                    Patch(facecolor=config.EXHALE_COLOR, alpha=config.SPAN_ALPHA + 0.15, label="exhale")]
    axes[0].legend(handles=handles, loc="upper right", framealpha=0.9)
    for ax in axes[1:]:
        ax.legend(loc="upper right", framealpha=0.9)

    axes[0].set_xlim(*(xlim if xlim else (times[0], times[-1])))
    if full_scale:
        axes[0].set_ylim(0, 4095)

    fig.canvas.draw()
    for raw_ax, smooth_ax in raw_smooth_pairs:
        smooth_ax.set_ylim(raw_ax.get_ylim())

    fig.tight_layout()
    if save:
        fig.savefig(save, dpi=image_dpi)
        print(f"이미지 저장: {save}")
    plt.show()


# -----------------------------------------------------------------------------
# 2) 1-pole vs 2차 Butterworth 대역통과 비교 (mV)
# -----------------------------------------------------------------------------
def plot_filter_compare(path, save=None, hp=config.HP_HZ, lp=config.LP_HZ, xlim=None,
                        shade=True, inhale_s=config.INHALE_S, exhale_s=config.EXHALE_S,
                        image_dpi=150):
    """mV 신호에 1-pole 과 2차 Butterworth 대역통과를 각각 통과시켜 3단으로 비교."""
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch

    use_korean_font()   # 파일 이름이 제목에 들어가므로 한글이 깨지지 않게

    if save is None:
        save = image_path_for(path, "_filters.png")
    times, _raws, mvs, phases = read_csv(path)
    fs = estimate_sample_rate(times)
    bp_onepole = bandpass_1pole(times, mvs, hp, lp)
    bp_butter = bandpass_butter2(mvs, fs, hp, lp)

    fig, (ax_mv, ax_op, ax_bw) = plt.subplots(3, 1, sharex=True, figsize=(13, 9))
    spans = target_spans(times, phases, inhale_s, exhale_s) if shade else []
    if spans:
        for ax in (ax_mv, ax_op, ax_bw):
            shade_axis(ax, spans)

    ax_mv.plot(times, mvs, linewidth=0.9, color="#333333", label="calibrated mV (raw)", zorder=2)
    ax_mv.set_ylabel("calibrated mV")
    ax_mv.grid(True, alpha=0.25, zorder=1)

    ax_op.axhline(0.0, color="#999999", linewidth=0.8, zorder=1)
    ax_op.plot(times, bp_onepole, linewidth=1.5, color=config.ONEPOLE_COLOR,
               label=f"1-pole BP ({hp}-{lp}Hz)", zorder=3)
    ax_op.set_ylabel("1-pole BP (mV)")
    ax_op.grid(True, alpha=0.25, zorder=1)

    ax_bw.axhline(0.0, color="#999999", linewidth=0.8, zorder=1)
    ax_bw.plot(times, bp_butter, linewidth=1.5, color=config.BUTTER_COLOR,
               label=f"2nd Butterworth BP ({hp}-{lp}Hz)", zorder=3)
    ax_bw.set_ylabel("2nd Butterworth BP (mV)")
    ax_bw.set_xlabel("time (s)")
    ax_bw.grid(True, alpha=0.25, zorder=1)

    ax_mv.set_title(f"{os.path.basename(path)}   |  mV band-pass: 1-pole vs 2nd Butterworth")

    handles, _ = ax_mv.get_legend_handles_labels()
    if spans:
        handles += [Patch(facecolor=config.INHALE_COLOR, alpha=config.SPAN_ALPHA + 0.15, label="inhale"),
                    Patch(facecolor=config.EXHALE_COLOR, alpha=config.SPAN_ALPHA + 0.15, label="exhale")]
    ax_mv.legend(handles=handles, loc="upper right", framealpha=0.9)
    ax_op.legend(loc="upper right", framealpha=0.9)
    ax_bw.legend(loc="upper right", framealpha=0.9)

    ax_mv.set_xlim(*(xlim if xlim else (times[0], times[-1])))

    fig.canvas.draw()
    common = (min(ax_op.get_ylim()[0], ax_bw.get_ylim()[0]),
              max(ax_op.get_ylim()[1], ax_bw.get_ylim()[1]))
    ax_op.set_ylim(common)
    ax_bw.set_ylim(common)

    fig.tight_layout()
    if save:
        fig.savefig(save, dpi=image_dpi)
        print(f"이미지 저장: {save}")
    plt.show()


# -----------------------------------------------------------------------------
# 3) 검출 결과 오버레이 (검출은 스크립트가 하고, 여기선 그리기만)
# -----------------------------------------------------------------------------
def plot_detection(times, mvs, y_bp, inhale_onsets, exhale_onsets, apnea_spans,
                   target_spans_list, bpm, hp, lp, title, save,
                   xlim=None, image_dpi=150):
    """위=mV 원본(+목표 음영), 아래=대역통과 신호 + 흡기(빨강)/호기(초록) 화살표."""
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch

    from matplotlib.lines import Line2D

    use_korean_font()   # 파일 이름이 제목에 들어가므로 한글이 깨지지 않게

    fig, (ax_mv, ax_bp) = plt.subplots(2, 1, sharex=True, figsize=(14, 8))

    if target_spans_list:
        shade_axis(ax_mv, target_spans_list)
    ax_mv.plot(times, mvs, linewidth=0.9, color="#333333", label="calibrated mV (raw)", zorder=2)
    ax_mv.set_ylabel("calibrated mV")
    ax_mv.grid(True, alpha=0.25, zorder=1)

    if target_spans_list:
        shade_axis(ax_bp, target_spans_list)
    for a, b in apnea_spans:
        ax_bp.axvspan(a, b, color=config.APNEA_COLOR, alpha=0.30, linewidth=0, zorder=0)
    ax_bp.axhline(0.0, color="#999999", linewidth=0.8, zorder=1)
    ax_bp.plot(times, y_bp, linewidth=1.4, color=config.BUTTER_COLOR,
               label=f"2nd Butterworth BP ({hp}-{lp}Hz)", zorder=3)

    amp_ref = (max(y_bp) - min(y_bp)) * 0.12
    _draw_arrows(ax_bp, inhale_onsets, config.INHALE_COLOR, +1, amp_ref)
    _draw_arrows(ax_bp, exhale_onsets, config.EXHALE_COLOR, -1, amp_ref)

    ax_bp.set_ylabel("band-pass (mV)")
    ax_bp.set_xlabel("time (s)")
    ax_bp.grid(True, alpha=0.25, zorder=1)
    ax_mv.set_title(title)

    mv_handles, _ = ax_mv.get_legend_handles_labels()
    if target_spans_list:
        mv_handles += [Patch(facecolor=config.INHALE_COLOR, alpha=config.SPAN_ALPHA + 0.15, label="target inhale"),
                       Patch(facecolor=config.EXHALE_COLOR, alpha=config.SPAN_ALPHA + 0.15, label="target exhale")]
    ax_mv.legend(handles=mv_handles, loc="upper right", framealpha=0.9)

    bp_handles = [Line2D([0], [0], color=config.BUTTER_COLOR, lw=1.4, label="Butterworth BP"),
                  Line2D([0], [0], color=config.INHALE_COLOR, lw=1.8, marker="^",
                         markersize=8, linestyle="None", label="inhale onset (rising)"),
                  Line2D([0], [0], color=config.EXHALE_COLOR, lw=1.8, marker="v",
                         markersize=8, linestyle="None", label="exhale onset (falling)")]
    if apnea_spans:
        bp_handles.append(Patch(facecolor=config.APNEA_COLOR, alpha=0.30, label="apnea"))
    ax_bp.legend(handles=bp_handles, loc="upper right", framealpha=0.9)

    ax_mv.set_xlim(*(xlim if xlim else (times[0], times[-1])))
    fig.tight_layout()
    if save:
        fig.savefig(save, dpi=image_dpi)
        print(f"이미지 저장: {save}")
    plt.show()
