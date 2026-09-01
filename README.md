# 객담 배출 보조 조끼

호기(내쉬는 숨)에 맞춰 흉부를 타진하는 객담 배출 보조 도구. 전도성 고무 센서로
호흡 주기를 실시간으로 감지하고, **호기 구간에만** 타진 모듈을 동작시킨다.
스마트폰 앱 혹은 유선 컨트롤러로 여러 설정이 가능하게 구현할 예정.

```
전도성 고무 센서 ──▶ 호흡 주기 검출 ──▶ 호기 구간에 타진 ──▶ 앱으로 상태 전송
  (고무 센서)       (ESP32-S3)       (DC 모터)          (BLE)
```

타진 타이밍이 이 시스템의 핵심 요소 중 하나이다. **흡기 중에 타진하면 안 되므로**, 호기가 시작된 것을
즉시 알아야 한다. 그래서 프로젝트의 첫 단계로 실시간 호흡 주기 검출을
만들었고, 지금은 ESP32-S3에서 스스로 판정한다.

## 구성 요소

| 기능 | 상태 | 위치 |
|---|---|---|
| **호흡 주기 검출** | ✅ 완료 — 기기에서 실시간 판정 | `firmware/prototype/task_sense.*`, `breath_*` |
| **타진 모듈 제어** | 예정 — 회로 먼저 | `task_app.cpp` 의 `TODO(1단계)` |
| **BLE 앱 연동** | 골격만 — 구조와 규칙 확정 | `link_ble.*`, `link_cmd.*` |

실시간 호흡 검출은 **파이썬과 C 두 곳에서 같은 알고리즘이 사용된다.** 파이썬(`breath/`)은
PC 에서 개발·분석할 때, C 이식본(`firmware/`)은 esp32-s3에서 동작한다. 둘이 다른 결과를 내지 않도록 저장된 CSV 파일을 양쪽에 입력으로 사용해 결과를 대조하는 검증 프로그램(`firmware/host_test/`)이 존재한다.

---

## 목차

- [구성 요소](#구성-요소)
- [디렉토리 / 파일 구조](#디렉토리--파일-구조)
  - [`firmware/` — 펌웨어](#firmware--펌웨어)
    - [`prototype/` — 현재 개발 중인 스케치](#prototype--현재-개발-중인-스케치)
    - [`breath_monitor/` — 구버전](#breath_monitor--호흡-검출만-구현된-구버전)
    - [`host_test/` — 검증 프로그램](#host_test--검증-프로그램-디렉토리)
    - [`conductive_rubber_cord.ino` — 초기 테스트용 파일](#conductive_rubber_cordino--초기-테스트용-파일)
  - [`breath/` — 파이썬 호흡 검출 라이브러리](#breath--파이썬-호흡-검출-라이브러리-펌웨어c-코드로-이식-완료)
  - [`plotting/` — 그래프 그리기](#plotting--개발-pc에서-센서값-및-필터링-이후-파형-그래프-그리기-검출-로직-없음)
  - [`scripts/` — 파이썬 실행 진입점](#scripts--파이썬-코드-실행-진입점-얇은-cli)
  - [기타 디렉토리](#기타-디렉토리)
- [호흡 주기 검출](#호흡-주기-검출)
  - [신호 처리 — 대역통과 필터](#신호-처리--대역통과-필터)
  - [검출 알고리즘 — 진폭 히스테리시스](#검출-알고리즘--진폭-히스테리시스-amplitudedetector)
  - [무호흡 판정](#무호흡-판정--전환-문턱과-분리된-별도-기준)
  - [검출 알고리즘 — 기울기](#검출-알고리즘--기울기-slopedetector)
  - [마커 위치와 검출 지연](#마커-위치와-검출-지연)
  - [파이썬 → C 이식](#파이썬--c-이식)
- [실행 순서](#실행-순서)
  - [실시간으로 보기](#실시간으로-보기)
- [앞으로 할 일](#앞으로-할-일)
  - [타진 모듈 (DC 모터)](#타진-모듈-dc-모터)
  - [BLE 앱 연동](#ble-앱-연동)

---

## 디렉토리 / 파일 구조

esp32-s3에서 동작을 위한 펌웨어 코드는 `firmware/prototype/` 에 존재

파이썬 코드 실행 파일(`scripts/`)과 내부 로직(`breath/`, `plotting/`)이 다른 폴더에 분리되어 있음. 
`scripts/`는 프로그램 실행만 하고, 실제 계산은 라이브러리가 한다.

```
Air-Rhythm/
├── firmware/
│   ├── prototype/       ★ 현재 개발 중인 스케치 — 듀얼코어 + BLE
│   ├── breath_monitor/  검출 이식이 끝난 시점의 동결 스냅샷 (구버전)
│   ├── host_test/       호스트에서 펌웨어를 재생·대조하는 검증 하네스
│   └── conductive_rubber_cord.ino   (초기 버전)
├── breath/              파이썬 순수 로직 라이브러리 (필터·검출·IO) — 펌웨어 이식 원본
│   └── detectors/        파이썬 검출 전략들 (전환 판정 상태기계)
├── plotting/            파이썬으로 그래프 그리기 위한 matplotlib 표현 계층 (그리기만)
├── scripts/             파이썬 코드 실행 진입점(CLI) — 얇은 조립 코드
├── data/                측정된 CSV 파일들
└── README.md
```

### `firmware/` — 펌웨어

esp32-s3가 실시간으로 호흡 주기를 판정한다. 호흡 주기 인지 기능을 core 1 전용 태스크에 격리하고,
상태 출력·BLE 통신·모터 제어는 core 0 에 둔다. 두 코어는 (현재로써는) 큐 하나로만 정보를 주고 받는다.

```
┌─ Core 1 ── 센서 전용 ────────────────┐   ┌─ Core 0 ── 나머지 전부 ────────┐
│  sense_task  priority 5 · 20ms     │   │  BLE 스택 (시스템)             │
│  ADC → 대역통과 → 검출                │──▶│  app_task  priority 2       │
│  Serial·BLE·모터 호출 금지            │ 큐 │  명령 반영 → 출력 → (모터)      │
└────────────────────────────────────┘   └─────────────────────────────┘
```

**왜 나누는가:** 호흡 인지 기능이 전부 `dt = 20ms, 50Hz (초당 50회 인지)`가 전제로 구현되어 있다.
`Serial.printf`나 BLE 통신이 같은 루프에 있으면
호흡 인지 주기가 지연될 수 있고, 그러면 **호흡 주기 검출이 오동작할 수 있다.**

#### `prototype/` — 현재 개발 중인 스케치

| 파일 | 역할 |
|---|---|
| `prototype.ino` | `setup()` 에서 큐·태스크 생성. `loop()` 는 재워둔다(core 1 을 센서에게 넘김) |
| `board_config.h` | GPIO 핀번호·센서 인지 주기·태스크 배치(코어/우선순위/스택)·큐 크기 |
| `app_types.h` | **코어 간 통신에 사용하는 자료형 정의** — `SenseUpdate`(센서→앱), `Command`(앱→센서) |
| `task_sense.h/.cpp` | **[core 1]** ADC → 대역통과 → 호흡 검출. 여기서 BLE 등을 사용하지 않는 것이 50Hz 를 지키는 조건 |
| `task_app.h/.cpp` | **[core 0]** 큐 소비 → 명령 반영 → 출력. 앞으로 BLE 연결과 모터 제어가 들어갈 자리 |
| `link_msg.h/.cpp` | ESP32에서 외부(앱, 시리얼 모니터)로 보낼 메시지를 문자열로 만들고, Serial 또는 BLE로 전달한다. 센서 이벤트, 오류, 명령 응답의 출력 형식을 관리한다 |
| `link_cmd.h/.cpp` | Serial과 BLE에서 들어온 문자열 명령을 `Command`로 변환하고 `q_cmd`에 전달한다. 하드웨어를 직접 제어하지 않는다 *(구현 필요)* |
| `link_ble.h/.cpp` | BLE GATT 서버를 초기화하고, 스마트폰의 명령을 `Command`로 전달하며, ESP32의 메시지를 BLE Notify로 보낸다. BLE 통신 상태만 관리하고 모터·센서 상태는 직접 관리하지 않는다 *(구현 필요)* |
| `breath_config.h`<br>`breath_filter.h/.cpp`<br>`breath_slope.h/.cpp` | 검출 로직 — `breath/` 의 C 이식본. **손대지 않는다** |

#### `breath_monitor/` — 호흡 검출만 구현된 구버전

- 호흡 검출 이식이 끝나고 아직 코어별 태스크를 나누기 전 상태
- 동작하는 기준점으로 남겨둔 것이며 앞으로의 기능 구현은 `prototype/` 에서 진행

#### `host_test/` — 검증 프로그램 디렉토리

ESP32-S3 없이 Arduino·FreeRTOS 를 pc에서 대신 실행하고 **펌웨어 소스를 그대로 링크**해, 저장된 CSV 파일을 입력으로 실행하며 출력을 대조한다. 실제 HW 없이 오류가 있는지 확인한다.

| 스크립트 | 무엇을 보장하나 |
|---|---|
| `run.sh` | 태스크를 나눈 구조(`drv_new`)와 나누기 전 구조(`drv_old`)의 출력이 **바이트 단위로 같은가** |
| `golden.sh` | 지금 검출 결과가 저장된 골든과 같은가. `breath_slope.cpp` 등을 고칠 때 쓴다 |

```bash
sh firmware/host_test/golden.sh            # 비교
sh firmware/host_test/golden.sh --update   # 골든 갱신 (의도한 변경일 때만)
```

> `run.sh` 는 두 드라이버가 같은 `breath_slope.cpp` 를 링크하므로 **그 파일 안의
> 오류는 잡지 못한다.** 그 빈틈을 `golden.sh` 가 메운다. 둘은 서로 다른 것을 검사한다.

#### `conductive_rubber_cord.ino` — 초기 테스트용 파일

`analogReadMilliVolts` 로 보정된 mV(및 raw)를 시리얼 모니터에 출력만 하던 버전. 호흡 주기 판정은
PC 가 했다. 지금은 esp32가 직접 판정하므로 참고용으로만 남아 있다.

### `breath/` — 파이썬 호흡 검출 라이브러리: 펌웨어(C 코드) 이식 완료

| 파일 | 역할 |
|---|---|
| `config.py` | 공용 상수 — 대역통과 기본 차단주파수(HP/LP), 호흡 페이싱 기본값, 색상 |
| `io_csv.py` | CSV 읽기(`read_csv`), 파일 선택(`ask_csv_file`), 흡기/호기 구간 계산(`spans_*`, `target_spans`), 이미지 경로 |
| `io_serial.py` | 시리얼 입력 — 포트 열기(`open_port`), 줄 파싱(`parse_sample`, 펌웨어 진단줄 `parse_diag`), 수신율 계량(`RateMeter`) |
| `filters.py` | 디지털 필터·스무딩. `bandpass_1pole`, 2차 Butterworth(`Biquad` → `BandpassButter2` 한 샘플씩 → `bandpass_butter2` 배열 래퍼), `moving_average_causal`, `ema`, `estimate_sample_rate` |
| `detectors/base.py` | 검출기 인터페이스 `Detector.update(t,y)->event` 와 배열 재생 러너 `run_detector` |
| `detectors/amplitude.py` | **진폭 히스테리시스 검출기** `AmplitudeDetector` — 골/마루에서 적응형 문턱만큼 되돌아오면 전환. 호흡률·무호흡 판정 포함 |
| `detectors/slope.py` | **기울기 검출기** `SlopeDetector` — 평활 기울기의 부호 전환으로 전환. 중점 게이트로 반대편 요철을 거른다. 지연이 더 낮다(무호흡 없음) |

### `plotting/` — 개발 pc에서 센서값 및 필터링 이후 파형 그래프 그리기 (검출 로직 없음)

| 파일 | 역할 |
|---|---|
| `plotting.py` | 세 가지 그래프: `plot_signals`(원본/스무딩/대역통과 다단), `plot_filter_compare`(1-pole vs Butterworth), `plot_detection`(대역통과 위 onset 화살표 — 검출 결과는 인자로 받음) |

### `scripts/` — 파이썬 코드 실행 진입점 (얇은 CLI)

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

| 디렉토리 | 역할 | 저장소 |
|---|---|---|
| `data/` | `log_serial.py` 가 남긴 측정 CSV | `breath_*.csv` 만 포함 |
| `images/` | 스크립트가 저장한 그래프 PNG | 제외
| `reference/` | 외부 참조 — 논문·특허·원본 스케치 | 제외 |
| `legacy/` | 대체된 옛 버전 | 제외 |

---

## 호흡 주기 검출

조끼가 **언제 타진할지**를 정하는 부분이다. 전도성 고무 센서의 파형에서
호기·흡기 전환 순간을 실시간으로 잡아낸다. 이 기능은 완성돼 기기에서 돌고 있으며,
아래는 그 원리와 근거다.

**미래 샘플을 참조하지 않는다.** 착용자가 숨을 내쉬기 시작한 뒤에 그것을 알아야
타진을 켤 수 있으므로, 신호 처리와 검출이 전부 인과적(causal)이다. 덕분에 저장된
CSV 를 다시 흘려보내면 실시간과 같은 결과가 나오고, 그것이 검증의 근거가 된다.

### 신호 처리 — 대역통과 필터

원신호에는 (1) 작고 빠른 잔노이즈와 (2) 전도성 고무 센서의 느린 드리프트(크리프)가
섞여 있다. **대역통과 필터**로 호흡 대역만 남긴다.

- **고역통과(0.08Hz)**: 느린 드리프트 제거 → 신호가 **0 중심**으로 진동.
- **저역통과(0.70Hz)**: 빠른 노이즈·심박 성분 제거.

두 가지 필터 구현(`breath/filters.py`)이 되어있고, 검출기는 필터를 교체하며 테스트 가능:

| 구현 | 특징 |
|---|---|
| `bandpass_1pole` | 극점 실수 → 링잉 없음. 매 샘플 실제 dt로 계수 재계산 |
| `bandpass_butter2` | **(현재 사용 중)** 스커트 급함·통과대역 평탄(호흡 성분 보존 우수). 큰 계단 뒤 링잉 |

> 필터는 모두 **단방향 IIR** 이다. 즉, 미래의 값을 사용하지 않고 과거의 값만을 사용한다. (호흡 주기를 실시간으로 검출해야 하기 때문)
> 화면의 곡선 = 실시간 출력.

---

### 검출 알고리즘 — 진폭 히스테리시스 (`AmplitudeDetector`)

`breath/detectors/amplitude.py` 의 `update(t, y)` 가 한 샘플씩 처리한다.
입력 `y` 는 대역통과된 0중심 호흡 파형.

#### (a) 진폭 추적 — 누설 포락선
```python
env_hi = max(y, env_hi - decay·dt)   # 위 포락선
env_lo = min(y, env_lo + decay·dt)   # 아래 포락선
amp    = env_hi - env_lo             # 최근(~6초) peak-to-peak
```

#### (b) 상태기계 — 골/마루에서 문턱만큼 되돌아오면 전환
```python
delta = max(K_DELTA · amp, DELTA_FLOOR)   # 전환 문턱
# EXHALE 중: (y - trough) > delta  →  흡기 시작 확정
# INHALE 중: (peak - y)  > delta  →  호기 시작 확정
```
- **기준점**은 전체 최대/최소가 아니라 **이번 호흡의 골/마루**(새 호흡마다 리셋).
- **문턱**은 골/마루 값의 30%가 아니라 **최근 진폭 `amp` 의 30%**.
- 안전장치: 전환 직후 반대 전환 금지(`MIN_PHASE_S`), 시작 과도응답 구간 판정 보류(`SETTLE_S`).

#### (c) 문턱을 진폭 비례로 두는 이유
문턱이 `K_DELTA · amp` 로 진폭에 비례하므로, **호흡이 약해지면 문턱도 같이 줄어
전환이 계속 일어난다.** 예외 둘: `DELTA_FLOOR`(절대 하한 아래로 약해지면 전환
멈춤 — 의도된 것), 포락선 지연(급변 시 한 박자 놓칠 수 있음).

#### (d) 호흡률
연속 흡기 onset 간격의 중앙값(최근 5개) → BPM.

---

### 무호흡 판정 — 전환 문턱과 분리된 별도 기준

| | 전환 문턱 | 무호흡 문턱 |
|---|---|---|
| 설정값 | `K_DELTA = 0.30` | `APNEA_FRAC = 0.30` |
| 비교 대상 | **이번 호흡의 골/마루** 대비 움직임 | **최근 진폭** vs **장기 기준선**(~30초 EMA) |
| 질문 | "신호가 방향을 틀었나?" | "전체 호흡 세기가 평소 대비 무너졌나?" |

두 문턱은 우연히 둘 다 0.30 이지만 **독립 설정값**이다. 덕분에 **서서히 약해지는
호흡**은 기준선이 같이 내려가 무호흡으로 오판하지 않고, **갑자기 지속적으로 무너질
때만** 무호흡으로 잡는다.

---

### 검출 알고리즘 — 기울기 (`SlopeDetector`)

진폭 방식은 극점 근처가 평평해 확정이 늦다(아래 참조). 기울기 방식은 **평활된
기울기의 부호 전환**으로 트리거해 지연을 줄인다. 극점 직후 기울기는 0을 빠르게(가파르게)
통과하므로 작은 문턱을 금방 넘기 때문이다. 무호흡 판정은 없다.

#### 동작 (`breath/detectors/slope.py`)
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

#### 진폭 vs 기울기 (같은 CSV 실측)

| | 진폭(`detect_breath.py`) | 기울기(`detect_slope.py`) |
|---|---|---|
| 검출 수 | 28회 | 28/29회 |
| 호흡률 | 14.6 bpm | 15.0 bpm |
| **평균 검출 지연** | **539 ms** | **230 ms** |

정확도는 동등하고 지연은 절반 이하. 단 정확도를 지키면 **~230ms가 현실적 바닥**이고,
100ms 이하는 반응형으로는 어려워 **주기 예측(feed-forward)** 이 필요하다.

---

### 마커 위치와 검출 지연

인과 검출기는 골을 지나 문턱만큼 되돌아오기 전엔 확신할 수 없어 확정이 항상 늦다.

- `ONSET_MARK = "confirm"` (기본, 제품용): **확정 순간**에 마커/트리거. 실시간에선 지나간 골로 돌아갈 수 없으므로 이것이 맞다.
- `ONSET_MARK = "extremum"`: 실제 골/마루로 소급(오프라인 분석용).

콘솔에 평균 검출 지연(극점→확정)이 출력된다. 진폭 방식은 극점 근처가 평평해
**~150ms 아래로는 못 내려간다**(문턱을 더 낮추면 노이즈로 헛전환 폭증). 그래서
**기울기 방식(`SlopeDetector`)** 으로 지연을 낮췄고(539→230ms), 그보다 더(≤100ms)
낮추려면 **주기 예측(feed-forward)** 이 필요하다(논문·특허 방식).

---

### 파이썬 → C 이식

`SlopeDetector` 경로는 **이식이 끝났다.** 기기가 스스로 판정하고 전환 순간을 시리얼로 알린다.

| 파이썬 | C (`firmware/prototype/`) |
|---|---|
| `breath/config.py` + `detectors/slope.py` 상수 | `breath_config.h` |
| `filters.Biquad`, `BandpassButter2` | `breath_filter.h/.cpp` |
| `detectors.SlopeDetector` + `monitor_breath.check_signal()` | `breath_slope.h/.cpp` |

> 두 구현의 이름을 1:1로 맞춰 두었다. 짧은 이름(`sd`, `env_hi`, `ext_val` …)의 뜻은
> `breath_slope.cpp` 맨 위 **"이름 풀이"** 블록에 모여 있다. 나란히 놓고 읽을 수
> 있어야 하므로 **한쪽만 바꾸지 않는다.**

#### 이식하며 바뀐 것

- **시간을 초가 아니라 샘플 수로** 다룬다. `dt` 가 상수(1/50s)라 EMA 계수가 컴파일 상수가
  되고, `micros()` 순환과 float 정밀도 저하가 원천적으로 사라진다.
- **대역통과 입력에서 첫 샘플을 뺀다.** 고역통과가 어차피 DC 를 지우므로 출력은 같지만,
  원신호가 ~1000mV 라 biquad 상태가 커지고 극점 반지름 0.9929 가 반올림을 ~140배 증폭한다.
  실측: 파이썬 대비 오차 0.057mV → **0.0017mV**.
- `bpm()` 의 흡기 시각은 무한 리스트가 아니라 12칸 링버퍼(보고용이라 검출엔 영향 없음).
- ESP32 는 `double` 이 소프트웨어 에뮬레이션이므로 전부 `float`(`bfloat` typedef).

#### 검증 — 녹음 CSV 로 대조

`firmware/host_test/` 가 이 일을 한다. 파라미터나 펌웨어 코드를 바꿀 때마다 돌릴 것.

```bash
sh firmware/host_test/golden.sh   # 검출 결과가 그대로인가
sh firmware/host_test/run.sh      # 태스크 분리가 출력을 바꾸지 않았는가
```

`breath_config.h` 의 `BREATH_USE_DOUBLE` 로 정밀도를 전환해 컴파일하면
**로직 오류와 float32 정밀도 문제가 분리**된다.

실측(`breath_20260727_205524.csv`, 4264샘플): **이벤트 46개, 종류 불일치 0,
33개 완전 일치 + 13개 1샘플(20ms) 차이.** `float` 와 `double` 결과가 동일해 정밀도는
문제가 아니며, 남은 1샘플 차이는 PC 가 호스트 타임스탬프를, 펌웨어가 고정 `dt` 를 쓰는
데서 온다(파이썬이 같은 CSV 를 재생해도 같은 크기의 차이가 난다).

> 이 대조가 실제로 `LP_HZ` 불일치(0.50 vs 0.70)를 잡아냈다. 상수를 한쪽만 고치면
> 바로 드러나므로, **파라미터를 바꿀 때마다 돌릴 것.**

#### 실기기에서 확인할 지표

`board_config.h` 의 `REPORT_RATE = true` 로 두면 1초에 한 줄씩 표본화 진단이 나온다.

```
# fs=50.00Hz avg=20000us min=19998us max=20003us
```

`avg` 가 20000µs 에 붙고 `min`/`max` 가 그 근처면 정상이다. **`vTaskDelayUntil` 이
절대 시각 기준이라 오차가 누적되지 않으므로 `avg` 는 거의 항상 멀쩡하다 —
흔들림은 `min`/`max` 에만 나타난다.** BLE·모터를 붙인 뒤 봐야 할 값은 이쪽이다.
`# QDROP` 이 뜨면 core 0 이 640ms 넘게 막혔다는 뜻이다.

---

## 실행 순서

```bash
# 1) 측정(하드웨어 연결) → data/breath_...csv 기록 + 페이싱 안내
python scripts/log_serial.py
```
```bash
# 2) 필터 형태 비교
python scripts/plot_signals.py    data/breath_YYYYMMDD_HHMMSS.csv
python scripts/compare_filters.py data/breath_YYYYMMDD_HHMMSS.csv
```
```bash
# 3) 호기/흡기 검출
python scripts/detect_slope.py    data/breath_YYYYMMDD_HHMMSS.csv   # 기울기 방식(저지연)
```

검출 그래프: 위=mV 원본 + 목표(페이싱) 음영, 아래=Butterworth 대역통과 +
**흡기 시작=붉은 화살표, 호기 시작=초록 화살표**, 무호흡=회색 구간.

### 실시간으로 보기

저장된 CSV 파일 없이 실시간 검출 결과를 확인 가능. CSV 도 함께 남으므로 나중에
`detect_slope.py` 를 실행해 실시간 결과와 비교할 수 있다.

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

---

## 앞으로 할 일

### 타진 모듈 (DC 모터)

`task_app.cpp` 의 `TODO(1단계)` 자리. 호기 중 동작, 흡기에 정지.

**소프트웨어보다 회로가 먼저다.** 모터가 esp32-s3의 전원(3.3V 핀)을 쓰면 모터 전류가
ADC 측정값에 그대로 실린다. 게다가 호기에만 도는 구조라 그 노이즈가 **호흡 주기와
동기화**되어, 대역통과 필터가 걸러내지 못하고 검출기가 신호로 오인할 수 있다.
별도 전원·플라이백 다이오드(모터 드라이버에 내장)·스타 그라운드를 먼저 확보할 것.

> 확인 방법: 스트랩을 책상에 가만히 둔 채 모터만 켜고 `amp` 를 본다.
> `MIN_AMP`(5mV) 근처로 올라가면 회로를 고쳐야 한다.

### BLE 앱 연동

- BLE 관련 함수 작성 시 `link_*` 파일에 구현
- BLE 관련 함수 작성 후 기능 추가는 `prototype.ino` 와 `task_app.*` 파일에 작성
- 필요 시 소스 파일 추가/수정 가능
- `link_ble.cpp` / `link_cmd.cpp` 에 골격과 `TODO(구현)` 가 있음 
- 앱에서 esp32-s3 를 제어하고, 호기·흡기 전환을 앱으로 확인하는 것이 목표 
- 구조와 지켜야 할 규칙은 코드 주석에 있지만, 반드시 지키지 않아도 됨
