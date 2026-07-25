"""호흡 검출 전략들. 모두 Detector 인터페이스(update(t,y)->event)를 따른다."""

from .base import Detector, run_detector
from .amplitude import AmplitudeDetector
from .slope import SlopeDetector

__all__ = ["Detector", "run_detector", "AmplitudeDetector", "SlopeDetector"]
