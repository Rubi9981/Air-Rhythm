"""CSV 입출력과 호흡 구간(span) 계산 — 순수 헬퍼.

log_serial 이 저장한 CSV(elapsed_s, raw, mv, phase, ...)를 읽고, 그래프
배경 음영에 쓸 흡기/호기 구간을 만든다. 플롯·필터와 무관하다.
"""

import csv
import glob
import os
import sys
from datetime import datetime


def list_csv_files():
    """측정 CSV 를 최근 저장 순으로 돌려준다. data/ 를 먼저, 없으면 현재 폴더."""
    files = (glob.glob("data/breath_*.csv") or glob.glob("data/*.csv")
             or glob.glob("breath_*.csv") or glob.glob("*.csv"))
    return sorted(files, key=os.path.getmtime, reverse=True)


def ask_csv_file():
    """터미널에서 그릴 CSV 파일을 입력받는다."""
    files = list_csv_files()
    if files:
        print("CSV 파일 (최근 순):")
        for i, name in enumerate(files, start=1):
            size_kb = os.path.getsize(name) / 1024
            mtime = datetime.fromtimestamp(os.path.getmtime(name))
            print(f"  [{i}] {name}   ({size_kb:,.0f} KB, {mtime:%m-%d %H:%M})")
        prompt = "\n번호 또는 파일 이름 (엔터 = 가장 최근): "
    else:
        print("CSV 파일을 찾지 못했습니다.")
        prompt = "\n그릴 CSV 파일 경로: "

    while True:
        try:
            answer = input(prompt).strip().strip('"').strip("'")
        except (EOFError, KeyboardInterrupt):
            sys.exit("\n취소했습니다.")
        if not answer:
            if files:
                print(f"-> {files[0]}")
                return files[0]
            print("파일 이름을 입력해 주세요.")
            continue
        if answer.isdigit() and files:
            n = int(answer)
            if 1 <= n <= len(files):
                print(f"-> {files[n - 1]}")
                return files[n - 1]
            print(f"1 ~ {len(files)} 사이의 번호를 입력하세요.")
            continue
        for candidate in (answer, answer + ".csv"):
            if os.path.exists(candidate):
                return candidate
        print(f"파일을 찾을 수 없습니다: {answer}")


def read_csv(path):
    """CSV에서 시간(초), raw, mv, 호흡 단계(phase)를 읽는다.

    mv 열이 없는 구버전 CSV는 raw 로 대체해 최소한 그려지게 한다.
    """
    times, raws, mvs, phases = [], [], [], []
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                t = float(row["elapsed_s"])
                raw = float(row["raw"])
            except (KeyError, ValueError, TypeError):
                continue
            try:
                mv = float(row["mv"])
            except (KeyError, ValueError, TypeError):
                mv = raw
            times.append(t)
            raws.append(raw)
            mvs.append(mv)
            phases.append((row.get("phase") or "").strip())
    if not times:
        sys.exit(f"{path} 에서 읽을 데이터가 없습니다.")
    return times, raws, mvs, phases


def spans_from_phases(times, phases):
    """기록된 phase 열에서 연속 구간 [(시작, 끝, 단계), ...] 을 만든다."""
    spans = []
    if not phases or not any(phases):
        return spans
    start = times[0]
    current = phases[0]
    for t, p in zip(times, phases):
        if p != current:
            spans.append((start, t, current))
            start, current = t, p
    spans.append((start, times[-1], current))
    return [s for s in spans if s[2] in ("inhale", "exhale")]


def spans_from_timing(t_end, inhale_s, exhale_s):
    """phase 열이 없을 때 흡기/호기 길이로 구간을 직접 계산한다."""
    spans = []
    cycle = inhale_s + exhale_s
    t = 0.0
    while t < t_end:
        spans.append((t, min(t + inhale_s, t_end), "inhale"))
        if t + inhale_s < t_end:
            spans.append((t + inhale_s, min(t + cycle, t_end), "exhale"))
        t += cycle
    return spans


def target_spans(times, phases, inhale_s, exhale_s):
    """phase 열이 있으면 그걸로, 없으면 페이싱 길이로 목표 구간을 만든다."""
    return spans_from_phases(times, phases) or spans_from_timing(times[-1], inhale_s, exhale_s)


def image_path_for(csv_path, ext=".png", out_dir="images"):
    """CSV 경로에서 같은 이름의 이미지 경로를 만든다(기본 images/ 폴더)."""
    base = os.path.splitext(os.path.basename(csv_path))[0]
    return os.path.join(out_dir, base + ext)
