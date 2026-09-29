"""펌웨어 검출기를 호스트에서 빌드·재생 — 기기에서 도는 C 코드 그대로 채점하기 위한 IO.

firmware/host_test/drv_eval.cpp 를 firmware/prototype/ 의 실제 소스(task_sense, breath_filter,
breath_slope)와 함께 빌드해 CSV 를 흘리고, 그 출력을 metrics.evaluate() 가 받는 모양으로
바꾼다. 조정값은 컴파일 옵션(-DK_SLOPE=0.3f)으로 바꾼다 — breath_config.h 가 #ifndef 로
감싸 둔 이름만 가능하다.

파이썬 검출기(breath/detectors)가 아니라 이것을 채점하는 이유: 기기는 float 로 계산하고
dt 를 1/50초로 고정하며 bpm 을 링버퍼로 낸다. 파이썬 검출기의 성적은 이 차이만큼 기기와
어긋날 수 있다.
"""

import atexit
import os
import shutil
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST_TEST = os.path.join(ROOT, "firmware", "host_test")
PROTOTYPE = os.path.join(ROOT, "firmware", "prototype")

# breath_config.h 에서 #ifndef 로 감싼 이름들. 여기 없는 이름을 -D 로 주면 재정의 경고만
# 나고 값이 조용히 무시되거나 빌드가 깨지므로 미리 막는다.
TUNABLE = ("HP_HZ", "LP_HZ", "SLOPE_TAU_S", "AVG_TAU_S", "K_SLOPE", "SLOPE_FLOOR",
           "MIN_PHASE_S", "MID_GATE", "PROM_RATIO", "MIN_AMP", "ENV_DECAY_S", "SETTLE_S",
           "POLARITY", "NO_SIGNAL_HYST", "RATE_WINDOW", "RATE_MIN_S", "RATE_MAX_S")
INT_TUNABLE = ("MID_GATE", "RATE_WINDOW")

FS_HZ = 50.0             # breath_config.h 의 FS_HZ. 기기는 이 간격을 전제로 계산한다
FLAG_SETTLED, FLAG_SIGNAL_OK = 1, 2
EVENT_INHALE, EVENT_EXHALE = 1, 2
PHASE_FALLING = -1

_build_dir = None
_cache = {}


def _c_literal(name, value):
    """-D 에 넣을 C 리터럴. 정수형은 그대로, 나머지는 float 접미사 f 를 붙인다."""
    v = str(value).strip()
    if name in INT_TUNABLE:
        return str(int(float(v)))
    v = repr(float(v))
    return v + "f"


def build(defines=None):
    """drv_eval 을 빌드해 실행 파일 경로를 돌려준다. 같은 조정값이면 다시 빌드하지 않는다."""
    global _build_dir
    defines = dict(defines or {})
    for name in defines:
        if name not in TUNABLE:
            raise ValueError(f"조정할 수 없는 이름: {name}  (가능: {', '.join(TUNABLE)})")
    key = tuple(sorted((k, _c_literal(k, v)) for k, v in defines.items()))
    if key in _cache:
        return _cache[key]

    if _build_dir is None:
        _build_dir = tempfile.mkdtemp(prefix="breath_eval_")
        atexit.register(shutil.rmtree, _build_dir, True)
    exe = os.path.join(_build_dir, f"drv_eval_{len(_cache)}")
    cxx = os.environ.get("CXX", "c++").split()
    cmd = cxx + ["-std=gnu++17", "-O2",
                 "-I" + os.path.join(HOST_TEST, "stub"), "-I" + PROTOTYPE, "-I" + HOST_TEST]
    cmd += [f"-D{k}=({v})" for k, v in key]      # 괄호: 음수(POLARITY=-1)도 식 안에서 안전
    cmd += [os.path.join(HOST_TEST, "drv_eval.cpp"), os.path.join(HOST_TEST, "stub_impl.cpp")]
    cmd += [os.path.join(PROTOTYPE, f) for f in
            ("task_sense.cpp", "breath_filter.cpp", "breath_slope.cpp")]
    cmd += ["-o", exe]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError("drv_eval 빌드 실패:\n" + res.stderr)
    _cache[key] = exe
    return exe


def unfit_reason(csv_path, times=None):
    """이 녹음을 펌웨어로 채점할 수 없는 이유. 문제 없으면 None.

    drv_eval 은 CSV 의 네 번째 열을 mV 로, 행 간격을 1/FS_HZ 로 믿는다. mv 열이 없는
    구버전 CSV 는 phase 글자를 mV 로 읽어 0 이 들어가고, 표본화율이 다르면 모든 시상수가
    그 비율만큼 틀어진다. 둘 다 오류 없이 돌기 때문에 여기서 걸러야 한다.
    """
    with open(csv_path, encoding="utf-8") as f:
        header = f.readline().strip().split(",")
    if len(header) < 4 or header[3] != "mv":
        return "mv 열 없음 (구버전 CSV)"
    if times is None:
        from .io_csv import read_csv
        times = read_csv(csv_path)[0]
    if len(times) < 2 or times[-1] - times[0] <= 0:
        return "샘플 부족"
    fs = (len(times) - 1) / (times[-1] - times[0])
    if abs(fs - FS_HZ) / FS_HZ > 0.03:
        return f"표본화율 {fs:.1f}Hz (펌웨어는 {FS_HZ:.0f}Hz 고정)"
    return None


def replay(exe, csv_path):
    """CSV 하나를 재생한다. 반환 dict 는 metrics.evaluate() 의 detection 인자 모양에
    n_total·config·ext(흡기/호기 확정 n → 검출기가 추적한 극점 n)를 더한 것."""
    res = subprocess.run([exe, csv_path], capture_output=True, text=True, check=True)
    config, changes, n_total = {}, [], None
    inhale, exhale, bpm, ext = [], [], [], {}
    for line in res.stdout.splitlines():
        tag, _, rest = line.partition(",")
        if tag == "C":
            for kv in rest.split(","):
                k, _, v = kv.partition("=")
                config[k] = float(v)
        elif tag == "S":
            n, phase, flags, events, ext_n, b = rest.split(",")
            n, phase, flags, events = int(n), int(phase), int(flags), int(events)
            changes.append((n, phase, flags))
            if events & EVENT_INHALE:
                inhale.append(n)
                ext[n] = int(ext_n)
                bpm.append((n, float(b)))
            if events & EVENT_EXHALE:
                exhale.append(n)
                ext[n] = int(ext_n)
        elif tag == "E":
            n_total = int(rest)
    if n_total is None:
        raise RuntimeError(f"drv_eval 출력이 끝나지 않음: {csv_path}")

    # 바뀐 샘플만 찍혀 있으므로 사이를 채운다
    phase_at, hit_at = [0] * n_total, [False] * n_total
    for (n, phase, flags), nxt in zip(changes, changes[1:] + [(n_total, 0, 0)]):
        ok = (flags & FLAG_SETTLED) and (flags & FLAG_SIGNAL_OK)
        hit = bool(ok and phase == PHASE_FALLING)
        for i in range(n, nxt[0]):
            phase_at[i] = phase
            hit_at[i] = hit
    return {"phase": phase_at, "hit": hit_at, "inhale": inhale, "exhale": exhale,
            "bpm": bpm, "ext": ext, "n_total": n_total, "config": config}
