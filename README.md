# 전도성 고무 호흡 센서 — 실시간 호기/흡기 검출

ESP32-S3 + 전도성 고무 스트레치 센서로 호흡 파형을 측정하고,
**실시간(인과적)으로 흡기/호기 전환과 무호흡을 판정**하는 프로젝트.

핵심 목표: "전환이 확정되는 순간" 특정 동작을 트리거하는 제품. 따라서 모든
신호 처리·검출은 **미래 샘플을 참조하지 않는 인과적 방식**이며, 기록된 CSV를
재생하면 실시간으로 들어왔을 때와 결과가 같다.

```
전도성 고무 → ADC(mV) → 대역통과 필터 → 진폭 추적 → 상태기계 → 흡기/호기 onset
   (원신호)   (노이즈+드리프트)  (0중심 물결)   (적응형 문턱)  (전환 확정)  (+ 호흡률/무호흡)
```

---

## 디렉토리 / 파일 구조

실행 파일(`scripts/`)과 내부 로직(`breath/`, `plotting/`)이 **층으로 분리**돼 있다.
`scripts/`는 "무엇을 할지"만 조립하고, 실제 계산은 라이브러리가 한다.

```
conductive-rubber/
├── firmware/        # ESP32 펌웨어(.ino)
├── breath/          # 순수 로직 라이브러리 (필터·검출·IO) — ESP32 이식 대상 경계
│   └── detectors/   #   검출 전략들 (전환 판정 상태기계)
├── plotting/             # matplotlib 표현 계층 (그리기만)
├── scripts/         # 실행 진입점(CLI) — 얇은 조립 코드
├── data/            # 측정 CSV
├── images/          # 출력 그래프(PNG)
├── reference/       # 외부 참조 코드·문헌
├── legacy/          # 대체된 옛 버전
└── README.md
```

### `firmware/` — 펌웨어

| 파일 | 역할 |
|---|---|
| `conductive_rubber_cord.ino` | ESP32-S3 스케치. `analogReadMilliVolts` 로 보정된 mV(및 raw)를 시리얼로 전송 |

### `breath/` — 순수 로직 라이브러리 (플롯·시리얼 무관, 펌웨어 이식 대상)

| 파일 | 역할 |
|---|---|
| `config.py` | 공용 상수 — 대역통과 기본 차단주파수(HP/LP), 호흡 페이싱 기본값, 색상 |
| `io_csv.py` | CSV 읽기(`read_csv`), 파일 선택(`ask_csv_file`), 흡기/호기 구간 계산(`spans_*`, `target_spans`), 이미지 경로 |
| `io_serial.py` | 시리얼 입력 — 포트 열기(`open_port`), 줄 파싱(`parse_sample`, 펌웨어 진단줄 `parse_diag`), 수신율 계량(`RateMeter`) |
| `filters.py` | 디지털 필터·스무딩. `bandpass_1pole`, 2차 Butterworth(`Biquad` → `BandpassButter2` 한 샘플씩 → `bandpass_butter2` 배열 래퍼), `moving_average_causal`, `ema`, `estimate_sample_rate` |
| `detectors/base.py` | 검출기 인터페이스 `Detector.update(t,y)->event` 와 배열 재생 러너 `run_detector` |
| `detectors/amplitude.py` | **진폭 히스테리시스 검출기** `AmplitudeDetector` — 골/마루에서 적응형 문턱만큼 되돌아오면 전환. 호흡률·무호흡 판정 포함 |
| `detectors/slope.py` | **기울기 검출기** `SlopeDetector` — 평활 기울기의 부호 전환으로 전환. 중점 게이트로 반대편 요철을 거른다. 지연이 더 낮다(무호흡 없음) |

> 두 검출기는 같은 `Detector.update(t,y)->event` 인터페이스와 이벤트 스키마를
> 쓰므로 `run_detector`·플롯·스크립트를 그대로 공유한다. 새 방식도 `Detector` 를
> 상속해 `update` 만 구현하면 된다.

### `plotting/` — 표현 계층 (그리기만, 검출 로직 없음)

| 파일 | 역할 |
|---|---|
| `plotting.py` | 세 가지 그래프: `plot_signals`(원본/스무딩/대역통과 다단), `plot_filter_compare`(1-pole vs Butterworth), `plot_detection`(대역통과 위 onset 화살표 — 검출 결과는 인자로 받음) |

### `scripts/` — 실행 진입점 (얇은 CLI)

| 파일 | 역할 | 실행 |
|---|---|---|
| `log_serial.py` | 시리얼(raw,mv) → `data/`에 CSV 기록 + 호흡 페이싱 안내 | `python scripts/log_serial.py` |
| `monitor_breath.py` | **실시간 검출 모니터** — 시리얼을 그 자리에서 필터·검출해 터미널에 표시 (+CSV 동시 기록) | `python scripts/monitor_breath.py` |
| `plot_signals.py` | raw/mV 원본·스무딩·대역통과 다단 비교 | `python scripts/plot_signals.py data/xxx.csv` |
| `compare_filters.py` | 1-pole vs 2차 Butterworth 대역통과 비교 | `python scripts/compare_filters.py data/xxx.csv` |
| `detect_breath.py` | **진폭 방식** 검출 → 화살표 그래프 (+무호흡) | `python scripts/detect_breath.py data/xxx.csv` |
| `detect_slope.py` | **기울기 방식** 검출 → 화살표 그래프 (저지연, 무호흡 없음) | `python scripts/detect_slope.py data/xxx.csv` |

각 스크립트 상단 `[설정]` 블록에 그 실행에만 관계된 조정값(창 크기·볼 구간·문턱 등)을 둔다.

### 기타 디렉토리

| 디렉토리 | 역할 |
|---|---|
| `data/` | `log_serial.py` 가 남긴 측정 CSV (`breath_YYYYMMDD_HHMMSS.csv`) |
| `images/` | 스크립트가 저장한 그래프 PNG |
| `reference/` | 외부 참조 — `arduino_breathing_monitor.ino`(Science Buddies 원본) |
| `legacy/` | 대체된 옛 버전(단일채널 로거·플롯, 초기 검출 실험 등) |

---

## 신호 처리 — 대역통과 필터

원신호에는 (1) 작고 빠른 잔노이즈와 (2) 전도성 고무의 느린 드리프트(크리프)가
섞여 있다. **대역통과 필터**로 호흡 대역만 남긴다.

- **고역통과(0.08Hz)**: 느린 드리프트 제거 → 신호가 **0 중심**으로 진동.
- **저역통과(0.50Hz)**: 빠른 노이즈·심박 성분 제거.

두 구현(`breath/filters.py`)이 있고 검출기는 어느 쪽이든 받는다:

| 구현 | 특징 |
|---|---|
| `bandpass_1pole` | 극점 실수 → 링잉 없음. 매 샘플 실제 dt로 계수 재계산 |
| `bandpass_butter2` | 스커트 급함·통과대역 평탄(호흡 성분 보존 우수). 큰 계단 뒤 링잉 |

> 필터는 모두 **단방향(causal) IIR** 이다(`filtfilt` 같은 양방향 아님).
> 화면의 곡선 = 실시간 출력.

---

## 검출 알고리즘 — 진폭 히스테리시스 (`AmplitudeDetector`)

`breath/detectors/amplitude.py` 의 `update(t, y)` 가 한 샘플씩 처리한다.
입력 `y` 는 대역통과된 0중심 호흡 파형.

### (a) 진폭 추적 — 누설 포락선
```python
env_hi = max(y, env_hi - decay·dt)   # 위 포락선
env_lo = min(y, env_lo + decay·dt)   # 아래 포락선
amp    = env_hi - env_lo             # 최근(~6초) peak-to-peak
```

### (b) 상태기계 — 골/마루에서 문턱만큼 되돌아오면 전환
```python
delta = max(K_DELTA · amp, DELTA_FLOOR)   # 전환 문턱
# EXHALE 중: (y - trough) > delta  →  흡기 시작 확정
# INHALE 중: (peak - y)  > delta  →  호기 시작 확정
```
- **기준점**은 전체 최대/최소가 아니라 **이번 호흡의 골/마루**(새 호흡마다 리셋).
- **문턱**은 골/마루 값의 30%가 아니라 **최근 진폭 `amp` 의 30%**.
- 안전장치: 전환 직후 반대 전환 금지(`MIN_PHASE_S`), 시작 과도응답 구간 판정 보류(`SETTLE_S`).

### (c) 문턱을 진폭 비례로 두는 이유
문턱이 `K_DELTA · amp` 로 진폭에 비례하므로, **호흡이 약해지면 문턱도 같이 줄어
전환이 계속 일어난다.** 예외 둘: `DELTA_FLOOR`(절대 하한 아래로 약해지면 전환
멈춤 — 의도된 것), 포락선 지연(급변 시 한 박자 놓칠 수 있음).

### (d) 호흡률
연속 흡기 onset 간격의 중앙값(최근 5개) → BPM.

---

## 무호흡 판정 — 전환 문턱과 분리된 별도 기준

| | 전환 문턱 | 무호흡 문턱 |
|---|---|---|
| 설정값 | `K_DELTA = 0.30` | `APNEA_FRAC = 0.30` |
| 비교 대상 | **이번 호흡의 골/마루** 대비 움직임 | **최근 진폭** vs **장기 기준선**(~30초 EMA) |
| 질문 | "신호가 방향을 틀었나?" | "전체 호흡 세기가 평소 대비 무너졌나?" |

두 문턱은 우연히 둘 다 0.30 이지만 **독립 설정값**이다. 덕분에 **서서히 약해지는
호흡**은 기준선이 같이 내려가 무호흡으로 오판하지 않고, **갑자기 지속적으로 무너질
때만** 무호흡으로 잡는다.

---

## 검출 알고리즘 — 기울기 (`SlopeDetector`)

진폭 방식은 극점 근처가 평평해 확정이 늦다(아래 참조). 기울기 방식은 **평활된
기울기의 부호 전환**으로 트리거해 지연을 줄인다. 극점 직후 기울기는 0을 빠르게(가파르게)
통과하므로 작은 문턱을 금방 넘기 때문이다. 무호흡 판정은 없다.

### 동작 (`breath/detectors/slope.py`)
```python
d  = (y - y_prev) / dt            # 1차 차분(기울기)
sd = ema(d, SLOPE_TAU_S)          # 기울기 평활 (지연↔노이즈 손잡이)
sth = max(K_SLOPE·평균|sd|, SLOPE_FLOOR)   # 적응형 데드밴드(Schmitt)
mid = (env_hi + env_lo) / 2       # 중점 게이트 기준(MID_GATE)
# FALLING 중 sd > +sth  이고 y < mid  →  흡기 시작 확정
# RISING  중 sd < -sth  이고 y > mid  →  호기 시작 확정
```
- **지연은 `SLOPE_TAU_S`(기울기 평활)가 지배** — 약하게 할수록 확정이 극점에 가까워진다.
- **정확도(double-hump 오검출)는 `MIN_PHASE_S`와 `MID_GATE`가 지배.** 둘 다 진폭
  되돌림으로 막는 것과 달리 **지연을 늘리지 않는다**.
  - `MIN_PHASE_S`: 전환 직후 그만큼 반대 전환을 막는다. "가장 짧은 반주기"보다
    작아야 한다(1.2s → 최대 ~25bpm, 0.6s → ~50bpm).
  - `MID_GATE`: 흡기 전환은 중점 아래, 호기 전환은 중점 위에서만 허용한다. 참 극점은
    항상 열린 쪽에 있으므로 참 전환은 막지 않고, 반대편 요철만 걸러낸다. 절대 시간이
    아니라 파형으로 판단하므로 **호흡 속도에 자동 적응**하며, 그만큼 `MIN_PHASE_S`를
    낮춰 대응 호흡률 상한을 넓힐 수 있다. 기준을 0 이 아니라 포락선 중점으로 두는 이유는
    `env_lo ≤ y ≤ env_hi` 라서 y 가 매 호흡 중점을 반드시 가로질러 **교착이 불가능**하고,
    I:E 비대칭도 함께 보정되기 때문이다(특허의 midpoint=½ p-p 와 같은 정의).
- `MIN_AMP` 아래(무신호)면 판정 보류. `PROM_RATIO>0`이면 골/마루에서 되돌림을 추가로
  요구(노이즈에 더 강하지만 지연↑, 기본 off).
- 이벤트 스키마가 진폭 방식과 같아 `run_detector`·`plot_detection`·`ONSET_MARK` 를 그대로 쓴다.

### 진폭 vs 기울기 (같은 CSV 실측)

| | 진폭(`detect_breath.py`) | 기울기(`detect_slope.py`) |
|---|---|---|
| 검출 수 | 28회 | 28/29회 |
| 호흡률 | 14.6 bpm | 15.0 bpm |
| **평균 검출 지연** | **539 ms** | **230 ms** |

정확도는 동등하고 지연은 절반 이하. 단 정확도를 지키면 **~230ms가 현실적 바닥**이고,
100ms 이하는 반응형으로는 어려워 **주기 예측(feed-forward)** 이 필요하다.

---

## 마커 위치와 검출 지연

인과 검출기는 골을 지나 문턱만큼 되돌아오기 전엔 확신할 수 없어 확정이 항상 늦다.

- `ONSET_MARK = "confirm"` (기본, 제품용): **확정 순간**에 마커/트리거. 실시간에선 지나간 골로 돌아갈 수 없으므로 이것이 맞다.
- `ONSET_MARK = "extremum"`: 실제 골/마루로 소급(오프라인 분석용).

콘솔에 평균 검출 지연(극점→확정)이 출력된다. 진폭 방식은 극점 근처가 평평해
**~150ms 아래로는 못 내려간다**(문턱을 더 낮추면 노이즈로 헛전환 폭증). 그래서
**기울기 방식(`SlopeDetector`)** 으로 지연을 낮췄고(539→230ms), 그보다 더(≤100ms)
낮추려면 **주기 예측(feed-forward)** 이 필요하다(논문·특허 방식).

---

## 파라미터 위치

- **공용 기본값**(차단주파수·색상·페이싱): `breath/config.py`
- **검출기 파라미터**(`K_DELTA`, `DELTA_FLOOR`, `SETTLE_S`, `APNEA_FRAC` …): `breath/detectors/amplitude.py` 의 모듈 상수(또는 `AmplitudeDetector(...)` 인자)
- **실행별 조정값**(볼 구간·창 크기·`ONSET_MARK` …): 각 `scripts/*.py` 상단 `[설정]` 블록

---

## 실행 순서

```bash
# 1) 측정(하드웨어 연결) → data/breath_...csv 기록 + 페이싱 안내
python scripts/log_serial.py

# 2) 필터 형태 비교
python scripts/plot_signals.py    data/breath_YYYYMMDD_HHMMSS.csv
python scripts/compare_filters.py data/breath_YYYYMMDD_HHMMSS.csv

# 3) 호기/흡기 onset 검출 → images/..._detect.png / _slope.png
python scripts/detect_breath.py   data/breath_YYYYMMDD_HHMMSS.csv   # 진폭 방식(+무호흡)
python scripts/detect_slope.py    data/breath_YYYYMMDD_HHMMSS.csv   # 기울기 방식(저지연)
```

검출 그래프: 위=mV 원본 + 목표(페이싱) 음영, 아래=Butterworth 대역통과 +
**흡기 시작=붉은 화살표, 호기 시작=초록 화살표**, 무호흡=회색 구간.

### 실시간으로 보기

기록·재생 없이 그 자리에서 검출 결과를 본다. CSV 도 함께 남으므로 나중에
`detect_slope.py` 로 재생해 실시간 결과와 대조할 수 있다.

```bash
python scripts/monitor_breath.py
```

```
  0:43.18   ▲ 흡기 시작   (지연  218ms)    15.2 bpm
  0:45.09   ▼ 호기 시작   (지연  241ms)    15.2 bpm
  0:49.71   ○ 신호 없음   (진폭  5.0mV — 스트랩 확인)
  1:10.21   ● 신호 복귀   (진폭  8.0mV)
  1:12  |  ▲ 흡기          |   15.2 bpm  |  진폭  24.4mV  |  50.0/50.0Hz  |  3350개
```

- 시작 전 `READY_S`(기본 5초) 준비 시간을 둔다. 그동안 도착하는 샘플은 읽어서 버린다 —
  그냥 기다리면 OS 버퍼에 쌓였다가 시작 직후 쏟아져 앞부분 `dt` 가 뭉친다.
- 흘러가는 줄 = **전환이 확정된 순간**(제품이 트리거될 시점). 지연은 극점→확정.
  무신호·무호흡 구간도 같은 자리에 남고, CSV 의 `event` 열에도 기록된다.
- 맨 아래 줄 = 현재 상태. `50.0/50.0Hz` 는 **기기가 보고한 주기 / 호스트 수신율**로,
  둘이 벌어지면 그 차이가 곧 유실된 샘플이다(`!` 표시).
- 앞 `SETTLE_S` 초는 `정착 중 … N초 남음` 으로 카운트다운하고 판정하지 않는다.
- `FS` 는 펌웨어의 `PERIOD_MS`(20ms → 50Hz)와 맞춰야 한다. 대역통과 계수가
  차단주파수/fs 비율로만 설계되므로, 어긋나면 차단주파수가 같은 비율로 밀린다.

> 실시간과 재생이 같은 결과를 내는 것은 **같은 `BandpassButter2`·`Detector` 를
> 쓰기 때문**이다(배열 함수는 이 클래스의 얇은 래퍼). 계산이 한 곳뿐이라 어긋날 수 없다.

---

## 펌웨어 이식 — `firmware/breath_monitor/`

`SlopeDetector` 경로는 **이식이 끝났다.** 기기가 스스로 판정하고 전환 순간을 시리얼로 알린다.

| 파일 | 대응 |
|---|---|
| `breath_config.h` | `breath/config.py` + `slope.py` 상수. **양쪽을 함께 고칠 것** |
| `breath_filter.h/.cpp` | `filters.Biquad`, `BandpassButter2` |
| `breath_slope.h/.cpp` | `detectors.SlopeDetector` + `monitor_breath.check_signal()` |
| `breath_monitor.ino` | 샘플링 루프 조립 + 이벤트 출력 |

### 이식하며 바뀐 것

- **시간을 초가 아니라 샘플 수로** 다룬다. `dt` 가 상수(1/50s)라 EMA 계수가 컴파일 상수가
  되고, `micros()` 순환과 float 정밀도 저하가 원천적으로 사라진다.
- **대역통과 입력에서 첫 샘플을 뺀다.** 고역통과가 어차피 DC 를 지우므로 출력은 같지만,
  원신호가 ~1000mV 라 biquad 상태가 커지고 극점 반지름 0.9929 가 반올림을 ~140배 증폭한다.
  실측: 파이썬 대비 오차 0.057mV → **0.0017mV**.
- `bpm()` 의 흡기 시각은 무한 리스트가 아니라 12칸 링버퍼(보고용이라 검출엔 영향 없음).
- ESP32 는 `double` 이 소프트웨어 에뮬레이션이므로 전부 `float`(`bfloat` typedef).

### 검증 — 녹음 CSV 로 대조

`breath_config.h` 의 `BREATH_USE_DOUBLE` 로 정밀도를 전환해 호스트에서 컴파일하면,
**로직 오류와 float32 정밀도 문제가 분리**된다. `data/*.csv` 의 `event` 열이 곧 정답지다.

```bash
c++ -O2 -Ifirmware/breath_monitor host_test.cpp \
    firmware/breath_monitor/breath_{filter,slope}.cpp -o t && ./t data/breath_*.csv
```

실측(`breath_20260727_205524.csv`, 4264샘플): **이벤트 46개, 종류 불일치 0,
33개 완전 일치 + 13개 1샘플(20ms) 차이.** `float` 와 `double` 결과가 동일해 정밀도는
문제가 아니며, 남은 1샘플 차이는 PC 가 호스트 타임스탬프를, 펌웨어가 고정 `dt` 를 쓰는
데서 온다(파이썬이 같은 CSV 를 재생해도 같은 크기의 차이가 난다).

> 이 대조가 실제로 `LP_HZ` 불일치(0.50 vs 0.70)를 잡아냈다. 상수를 한쪽만 고치면
> 바로 드러나므로, 파라미터를 바꿀 때마다 돌릴 것.

### 남은 것

- `estimate_sample_rate()` 는 오프라인 편의용이며, 펌웨어에선 상수 fs(50Hz)를 쓴다.
  `vTaskDelayUntil` 로 정확히 20ms 주기를 지키므로 상수로 두어도 된다
  (`delay(20)` 은 작업 시간이 주기에 더해져 45.5Hz 가 되니 쓰지 말 것).
- `AmplitudeDetector`(무호흡)는 아직 이식하지 않았다. `update(t, y)` 가 같은 구조라
  필요해지면 같은 방식으로 옮길 수 있다.
- 방법론 근거:
  - 논문: Sang et al., *Biosensors* 2024, 14, 118 — 램프 방향(기울기)으로 흡기/호기.
  - 특허: US 11,324,950 B2 (Inspire Medical) — 미분 + moving baseline/midpoint 로 onset,
    peak-to-peak vs moving baseline 로 무호흡, I:E 비대칭으로 극성.
