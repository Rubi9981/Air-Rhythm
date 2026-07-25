"""검출기 공통 인터페이스와 이벤트 재생 러너.

새 검출 방식(예: 기울기 0교차)을 추가할 때는 Detector 를 상속해
update(t, y) 만 구현하면, run_detector·플롯·스크립트를 그대로 재사용할 수 있다.
"""


class Detector:
    """호흡 검출기 인터페이스. update(t, y) 를 한 샘플씩 호출한다.

    반환: 이벤트 dict 또는 None. 이벤트 type 은
      'inhale_onset' | 'exhale_onset' | 'apnea_start' | 'apnea_end'
    onset 이벤트는 확정시각 t/y 와 실제 극점 ext_t/ext_y 를 함께 담는다
    (지연 = t - ext_t). 미래 샘플을 참조하지 않는다(인과적).
    """

    def update(self, t, y):
        raise NotImplementedError


def run_detector(detector, times, values, mark="confirm"):
    """검출기에 신호를 한 샘플씩 흘려 결과를 모은다(인과적 재생).

    mark="confirm"  : 마커를 전환 확정 시점(제품 액션 시점)에.
    mark="extremum" : 마커를 실제 골/마루로 소급.
    반환: (inhale_onsets, exhale_onsets, apnea_spans, delays)
      onsets 는 (t, y) 튜플 목록, delays 는 각 전환 지연[초] 목록.
    """
    inhale, exhale, apnea_spans, delays = [], [], [], []
    apnea_open = None
    for t, y in zip(times, values):
        ev = detector.update(t, y)
        if not ev:
            continue
        typ = ev["type"]
        if typ in ("inhale_onset", "exhale_onset"):
            delays.append(ev["t"] - ev["ext_t"])
            point = (ev["t"], ev["y"]) if mark == "confirm" else (ev["ext_t"], ev["ext_y"])
            (inhale if typ == "inhale_onset" else exhale).append(point)
        elif typ == "apnea_start":
            apnea_open = ev["t"]
        elif typ == "apnea_end" and apnea_open is not None:
            apnea_spans.append((apnea_open, ev["t"]))
            apnea_open = None
    if apnea_open is not None and len(times):
        apnea_spans.append((apnea_open, times[-1]))
    return inhale, exhale, apnea_spans, delays
