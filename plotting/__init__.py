"""plotting — matplotlib 표현 계층. 신호를 그리기만 하고 검출 로직은 담지 않는다.

세 그리기 함수를 패키지 최상위로 재노출해 `from plotting import plot_detection`
처럼 쓸 수 있게 한다.
"""

from .plotting import plot_signals, plot_filter_compare, plot_detection, shade_axis

__all__ = ["plot_signals", "plot_filter_compare", "plot_detection", "shade_axis"]
