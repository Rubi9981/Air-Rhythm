"""검출 결과를 기준 라벨과 맞대어 성적을 매긴다 — 순수 계산.

입력은 모두 샘플 번호(n) 기준이다. 검출 결과가 파이썬 검출기에서 왔든 펌웨어
빌드(drv_eval)에서 왔든 같은 모양으로 넘기면 같은 방식으로 채점된다.

이벤트 지표 (흡기·호기 따로):
  기준 극점 r 과 같은 종류의 검출 d 를, 지연 d - r 이 [-TOL_EARLY_S, +TOL_LATE_S] 안일
  때 1:1 로 짝짓는다(시간순 탐욕). 짝 없는 기준 = 놓침(FN), 짝 없는 검출 = 오검출(FP).
  지연은 짝지어진 것만으로 중앙값·p90·최대를 낸다. 검출기가 스스로 보고하는
  delay(= 확정 - 자기가 추적한 극점)와 달리, 기준 극점에서 잰 진짜 지연이다.

샘플 지표 (타진 안전):
  타진 가능 = 검출 위상이 호기이고 정착·신호 정상. breath_replay 의 안전 규칙 1 과 같은
  조건이며, 실제 모터는 상태 머신이 여기서 더 줄이므로 이것은 상한이다.
    흡기타진  타진 가능 샘플 중 기준이 흡기였던 비율   ← 낮을수록 안전
    호기커버  기준 호기 샘플 중 타진 가능이었던 비율   ← 높을수록 치료 시간이 길다
    무호흡타진 타진 가능 샘플 중 기준이 무호흡이었던 비율
"""

from statistics import median

TOL_EARLY_S = 0.3        # 기준 극점보다 이만큼 먼저 확정돼도 인정 (기준 라벨 오차 여유)
TOL_LATE_S = 1.0         # 이보다 늦은 확정은 놓침 + 오검출로 센다

PHASE_RISING, PHASE_FALLING, PHASE_UNKNOWN = +1, -1, 0


def ref_phase_per_sample(labels, n_total):
    """라벨 [(n, type)] → 샘플마다의 기준 상태 목록. 첫 라벨 이전은 'skip'."""
    out = ["skip"] * n_total
    for (n, typ), nxt in zip(labels, labels[1:] + [(n_total, None)]):
        for i in range(max(0, n), min(n_total, nxt[0])):
            out[i] = typ
    return out


def match_events(ref_ns, det_ns, fs, tol_early_s=TOL_EARLY_S, tol_late_s=TOL_LATE_S):
    """기준·검출 샘플 번호 목록을 1:1 로 짝짓는다. 반환: (짝 [(r, d)], 놓친 r, 남은 d)."""
    lo, hi = -tol_early_s * fs, tol_late_s * fs
    dets = sorted(det_ns)
    used = [False] * len(dets)
    pairs, missed = [], []
    for r in sorted(ref_ns):
        hit = None
        for j, d in enumerate(dets):
            if used[j]:
                continue
            if d - r > hi:
                break
            if d - r >= lo:
                hit = j
                break
        if hit is None:
            missed.append(r)
        else:
            used[hit] = True
            pairs.append((r, dets[hit]))
    extra = [d for d, u in zip(dets, used) if not u]
    return pairs, missed, extra


def percentile(values, q):
    """최근접 순위 백분위. 값이 없으면 None."""
    if not values:
        return None
    s = sorted(values)
    return s[min(len(s) - 1, max(0, int(round(q / 100.0 * (len(s) - 1)))))]


def evaluate(labels, detection, n_total, fs, eval_start_n, eval_end_n):
    """한 녹음의 원자료(개수·지연 목록)를 모은다. 비율 계산은 summarize() 가 한다.

    labels     : [(n, type)]  기준 라벨
    detection  : {'phase': [샘플별 위상], 'hit': [샘플별 타진 가능 bool],
                  'inhale': [확정 n], 'exhale': [확정 n], 'bpm': [(n, bpm)]}
    eval_*_n   : 평가 구간 [start, end). 정착 구간과 녹음 끝은 빼고 부른다
    """
    ref = ref_phase_per_sample(labels, n_total)

    def in_eval(n):
        return eval_start_n <= n < eval_end_n and ref[n] != "skip"

    raw = {"events": {}, "samples": {}, "bpm_err": []}
    for kind in ("inhale", "exhale"):
        ref_ns = [n for n, t in labels if t == kind and in_eval(n)]
        det_ns = [n for n in detection[kind] if in_eval(n)]
        pairs, missed, extra = match_events(ref_ns, det_ns, fs)
        raw["events"][kind] = {
            "tp": len(pairs), "fn": len(missed), "fp": len(extra),
            "latency_s": [(d - r) / fs for r, d in pairs],
        }

    hit = detection["hit"]
    s = {"hit": 0, "hit_inhale": 0, "hit_none": 0, "exhale": 0, "exhale_hit": 0}
    for i in range(eval_start_n, min(eval_end_n, n_total)):
        r = ref[i]
        if r == "skip":
            continue
        if hit[i]:
            s["hit"] += 1
            s["hit_inhale"] += r == "inhale"
            s["hit_none"] += r == "none"
        if r == "exhale":
            s["exhale"] += 1
            s["exhale_hit"] += bool(hit[i])
    raw["samples"] = s

    # 호흡률: 검출기가 bpm 을 낸 시점마다, 그 직전 기준 흡기 간격 5개의 중앙값과 비교
    ref_inh = [n for n, t in labels if t == "inhale"]
    for n, bpm in detection["bpm"]:
        if bpm <= 0 or not in_eval(n):
            continue
        prev = [x for x in ref_inh if x <= n][-6:]
        gaps = [b - a for a, b in zip(prev, prev[1:])]
        if len(gaps) >= 2:
            raw["bpm_err"].append(abs(bpm - 60.0 * fs / median(gaps)))
    return raw


def merge(raws):
    """여러 녹음의 원자료를 합친다(합산 성적용)."""
    out = {"events": {}, "samples": {}, "bpm_err": []}
    for kind in ("inhale", "exhale"):
        e = {"tp": 0, "fn": 0, "fp": 0, "latency_s": []}
        for r in raws:
            for k in ("tp", "fn", "fp"):
                e[k] += r["events"][kind][k]
            e["latency_s"] += r["events"][kind]["latency_s"]
        out["events"][kind] = e
    keys = ("hit", "hit_inhale", "hit_none", "exhale", "exhale_hit")
    out["samples"] = {k: sum(r["samples"][k] for r in raws) for k in keys}
    for r in raws:
        out["bpm_err"] += r["bpm_err"]
    return out


def _ratio(a, b):
    return a / b if b else None


def summarize(raw):
    """원자료 → 성적표 dict. 값을 낼 수 없으면 None."""
    out = {}
    for kind in ("inhale", "exhale"):
        e = raw["events"][kind]
        se = _ratio(e["tp"], e["tp"] + e["fn"])
        ppv = _ratio(e["tp"], e["tp"] + e["fp"])
        if se is None or ppv is None:
            f1 = None
        else:
            f1 = 2 * se * ppv / (se + ppv) if se + ppv else 0.0
        lat = e["latency_s"]
        out[kind] = {
            "tp": e["tp"], "fn": e["fn"], "fp": e["fp"],
            "sensitivity": se, "ppv": ppv, "f1": f1,
            "latency_median_s": median(lat) if lat else None,
            "latency_p90_s": percentile(lat, 90),
            "latency_max_s": max(lat) if lat else None,
        }
    s = raw["samples"]
    out["hit_in_inhale"] = _ratio(s["hit_inhale"], s["hit"])
    out["hit_in_none"] = _ratio(s["hit_none"], s["hit"])
    out["exhale_coverage"] = _ratio(s["exhale_hit"], s["exhale"])
    err = raw["bpm_err"]
    out["bpm_mae"] = sum(err) / len(err) if err else None
    return out
