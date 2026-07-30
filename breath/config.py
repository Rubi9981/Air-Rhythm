"""프로젝트 공용 상수 — 여러 모듈이 함께 쓰는 기본값과 색상.

특정 실행에만 관계된 조정값(창 크기, 볼 구간 등)은 여기가 아니라 각
scripts/*.py 상단의 [설정] 블록에 둔다.
"""

# --- 대역통과 기본 차단주파수 (Hz) ---
HP_HZ = 0.08          # 고역: 약 5 bpm (드리프트 제거)
LP_HZ = 0.70          # 저역: 약 42 bpm (노이즈 제거)

# --- 호흡 페이싱 기본값 (초) ---
# 측정 시 안내와, phase 열이 없는 CSV의 목표 음영 계산에 쓰인다.
INHALE_S = 1.5
EXHALE_S = 2.5

# --- 색상 ---
INHALE_COLOR  = "#d62728"   # 흡기 = 빨강
EXHALE_COLOR  = "#2ca02c"   # 호기 = 초록
APNEA_COLOR   = "#7f7f7f"    # 무호흡 = 회색
ONEPOLE_COLOR = "#9467bd"   # 1-pole 대역통과 = 보라
BUTTER_COLOR  = "#d62728"   # 2차 Butterworth = 빨강
SPAN_ALPHA    = 0.13         # 배경 음영 투명도
