package com.example.myapplication.model

enum class RespirationPhase(val displayName: String) {
    INSPIRATION("흡기"),
    EXHALATION("호기")
}

enum class IntensityLevel(val displayName: String) {
    LOW("약"),
    MEDIUM("중"),
    HIGH("강")
}

enum class DetectionMode(val title: String, val subtitle: String) {
    DETECTION("호흡 감지 모드", "환자 자가호흡 실시간 추적"),
    GENERAL("일반 모드", "일반 타진")
}

data class BleDeviceItem(
    val id: String,
    val name: String,
    val rssi: Int,
    val isConnected: Boolean = false,
    val isKnown: Boolean = true
)

enum class NavTab {
    DASHBOARD,
    PAIRING
}
