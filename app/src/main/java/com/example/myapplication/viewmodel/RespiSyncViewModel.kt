package com.example.myapplication.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.myapplication.ble.BleConnectionState
import com.example.myapplication.ble.BleManager
import com.example.myapplication.model.BleDeviceItem
import com.example.myapplication.model.DetectionMode
import com.example.myapplication.model.IntensityLevel
import com.example.myapplication.model.NavTab
import com.example.myapplication.model.RespirationPhase
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

// ============================================================================
// [PhaseSegment] — 파형 버퍼 내 흡기/호기 구간 정보
// startIndex : waveformBuffer 내 이 구간이 시작하는 인덱스
// endIndex   : -1 이면 현재 진행 중(아직 끝나지 않음), 그 외는 끝 인덱스(포함)
// ============================================================================
data class PhaseSegment(
    val phase: RespirationPhase,
    val startIndex: Int,
    val endIndex: Int = -1  // -1 = 현재 진행 중
)

data class RespiUiState(
    val isStarted: Boolean = false,
    val isConnected: Boolean = false,
    val connectedDeviceName: String = "ESP32_Vest_BLE",
    val batteryLevel: Int = 100,
    val isScanning: Boolean = false,
    val autoReconnect: Boolean = true,
    val deviceList: List<BleDeviceItem> = emptyList(),
    val currentTab: NavTab = NavTab.PAIRING, // 첫 화면을 기기연결(PAIRING)로 설정
    val isDarkTheme: Boolean = false,
    val showSettingsDialog: Boolean = false,

    // Telemetry & 호흡 인식 결과
    val respirationRate: Int = 16,
    val ieRatio: String = "1 : 2.0",
    val currentPhase: RespirationPhase = RespirationPhase.EXHALATION,
    val detectionMode: DetectionMode = DetectionMode.DETECTION,
    val intensity: IntensityLevel = IntensityLevel.MEDIUM,
    val deviceStatus: String = "연결 대기 중",
    val chestPressure: Int = 0,
    val pressureHistory: List<Float> = emptyList(),

    // 실시간 파형 그래프용
    // waveformBuffer : ESP32의 filt 값(대역통과 필터링된 호흡 파형) 최대 300개 (약 15초 @ 20Hz)
    //                  최신 값이 리스트 끝(우측), 과거 값이 앞(좌측)
    val waveformBuffer: List<Float> = emptyList(),
    // phaseSegments  : waveformBuffer 내 각 흡기/호기 구간의 인덱스 범위
    //                  최대 10개 구간만 유지 (오래된 것 자동 삭제)
    val phaseSegments: List<PhaseSegment> = emptyList()
)

private const val WAVEFORM_BUFFER_SIZE = 300   // 15초 @ 20Hz
private const val MAX_PHASE_SEGMENTS   = 10    // 유지할 최대 구간 수

class RespiSyncViewModel(application: Application) : AndroidViewModel(application) {
    private val bleManager = BleManager.getInstance(application)

    private val _uiState = MutableStateFlow(RespiUiState())
    val uiState: StateFlow<RespiUiState> = _uiState.asStateFlow()

    private var lastPhaseChangeTime = System.currentTimeMillis()
    private var inhaleDurationMs = 1200L
    private var exhaleDurationMs = 2400L
    private val pressureBuffer = mutableListOf<Float>()

    // 파형 버퍼 및 구간 세그먼트 (내부 가변 상태)
    private val waveBuffer = mutableListOf<Float>()
    private val phaseSegs  = mutableListOf<PhaseSegment>()

    init {
        observeBleConnection()
        observeBleTelemetry()
        observeScannedDevices()
        observeScanningState()
    }

    private fun observeBleConnection() {
        viewModelScope.launch {
            bleManager.connectionState.collect { connState ->
                val isConn = connState == BleConnectionState.CONNECTED
                _uiState.update { current ->
                    current.copy(
                        isConnected = isConn,
                        deviceStatus = when (connState) {
                            BleConnectionState.CONNECTED    -> "정상 작동 중"
                            BleConnectionState.CONNECTING   -> "연결 시도 중..."
                            BleConnectionState.RECONNECTING -> "재연결 중..."
                            BleConnectionState.DISCONNECTING -> "연결 해제 중..."
                            else -> "연결 해제됨"
                        }
                    )
                }
                // 연결 해제 시 파형 버퍼 초기화
                if (!isConn) {
                    waveBuffer.clear()
                    phaseSegs.clear()
                    _uiState.update { it.copy(waveformBuffer = emptyList(), phaseSegments = emptyList()) }
                }
            }
        }
    }

    private fun observeBleTelemetry() {
        viewModelScope.launch {
            bleManager.telemetryData.collect { telemetry ->
                val now = System.currentTimeMillis()

                // 1. 호흡 위상(Inhale/Exhale) 실시간 수신 및 자동 업데이트
                val newPhase = when (telemetry.respirationPhase) {
                    com.example.myapplication.ble.RespirationPhase.INHALE -> RespirationPhase.INSPIRATION
                    com.example.myapplication.ble.RespirationPhase.EXHALE -> RespirationPhase.EXHALATION
                    else -> _uiState.value.currentPhase
                }

                // 2. 호흡 위상 전환 감지 → I:E 비율 및 RR 계산 + 구간 세그먼트 기록
                val prevPhase = _uiState.value.currentPhase
                if (newPhase != prevPhase) {
                    val duration = now - lastPhaseChangeTime
                    if (prevPhase == RespirationPhase.INSPIRATION) {
                        inhaleDurationMs = duration.coerceAtLeast(500L)
                    } else {
                        exhaleDurationMs = duration.coerceAtLeast(500L)
                    }
                    lastPhaseChangeTime = now

                    // 현재 진행 중인 구간의 endIndex 확정
                    val currentBufSize = waveBuffer.size
                    if (phaseSegs.isNotEmpty() && phaseSegs.last().endIndex == -1) {
                        val lastSeg = phaseSegs.last()
                        phaseSegs[phaseSegs.size - 1] = lastSeg.copy(
                            endIndex = (currentBufSize - 1).coerceAtLeast(lastSeg.startIndex)
                        )
                    }
                    // 새 구간 시작 기록
                    phaseSegs.add(PhaseSegment(phase = newPhase, startIndex = currentBufSize))
                }

                // 3. filt 값(필터링된 호흡 파형)을 waveformBuffer에 추가
                //    chestPressure = sense->filt (signed int16, 이미 BleManager에서 signed 변환 완료)
                waveBuffer.add(telemetry.chestPressure.toFloat())

                // 버퍼 크기 초과 시 앞에서 제거 (슬라이딩 윈도우)
                val overflowCount = (waveBuffer.size - WAVEFORM_BUFFER_SIZE).coerceAtLeast(0)
                if (overflowCount > 0) {
                    waveBuffer.subList(0, overflowCount).clear()
                    // 구간 세그먼트 인덱스도 오프셋 보정
                    val iter = phaseSegs.iterator()
                    val updated = mutableListOf<PhaseSegment>()
                    while (iter.hasNext()) {
                        val seg = iter.next()
                        val newStart = seg.startIndex - overflowCount
                        val newEnd   = if (seg.endIndex == -1) -1 else seg.endIndex - overflowCount
                        if (newEnd != -1 && newEnd < 0) {
                            // 이 구간은 완전히 버퍼 밖으로 밀려남 → 삭제
                        } else {
                            updated.add(seg.copy(startIndex = newStart.coerceAtLeast(0), endIndex = newEnd))
                        }
                    }
                    phaseSegs.clear()
                    phaseSegs.addAll(updated)
                }

                // 오래된 세그먼트 최대 개수 제한
                if (phaseSegs.size > MAX_PHASE_SEGMENTS) {
                    phaseSegs.subList(0, phaseSegs.size - MAX_PHASE_SEGMENTS).clear()
                }

                // 4. 레거시 pressureBuffer (기존 호환용, 유지)
                pressureBuffer.add(telemetry.chestPressure.toFloat())
                if (pressureBuffer.size > 100) pressureBuffer.removeAt(0)

                // 5. I:E 비율 / RR 계산
                val totalCycleMs = (inhaleDurationMs + exhaleDurationMs).toFloat()
                val calculatedRR = if (totalCycleMs > 0) (60000f / totalCycleMs).toInt().coerceIn(8, 30) else 16
                val ratioVal = if (inhaleDurationMs > 0)
                    String.format("%.1f", exhaleDurationMs.toFloat() / inhaleDurationMs) else "2.0"

                _uiState.update { current ->
                    current.copy(
                        batteryLevel     = telemetry.powerStatus,
                        currentPhase     = newPhase,
                        respirationRate  = calculatedRR,
                        ieRatio          = "1 : $ratioVal",
                        chestPressure    = telemetry.chestPressure,
                        pressureHistory  = pressureBuffer.toList(),
                        waveformBuffer   = waveBuffer.toList(),
                        phaseSegments    = phaseSegs.toList()
                    )
                }
            }
        }
    }

    private fun observeScannedDevices() {
        viewModelScope.launch {
            bleManager.scannedDevices.collect { devices ->
                _uiState.update { current ->
                    current.copy(
                        deviceList = devices.map {
                            BleDeviceItem(
                                id = it.address,
                                name = it.name,
                                rssi = it.rssi,
                                isConnected = it.address == bleManager.scannedDevices.value.find { d -> bleManager.isConnected() }?.address
                            )
                        }
                    )
                }
            }
        }
    }

    private fun observeScanningState() {
        viewModelScope.launch {
            bleManager.isScanning.collect { scanning ->
                _uiState.update { it.copy(isScanning = scanning) }
            }
        }
    }

    // --- 제어 및 상태 토글 ---
    fun startApp() { _uiState.update { it.copy(isStarted = true) } }
    fun setTab(tab: NavTab) { _uiState.update { it.copy(currentTab = tab) } }
    fun toggleDarkTheme(isDark: Boolean) { _uiState.update { it.copy(isDarkTheme = isDark) } }
    fun setSettingsDialogVisible(visible: Boolean) { _uiState.update { it.copy(showSettingsDialog = visible) } }

    fun startScan() { bleManager.startScan() }
    fun connectDevice(address: String) {
        val device = bleManager.scannedDevices.value.find { it.address == address }
        device?.let { bleManager.connectToDevice(it) }
    }
    fun disconnectDevice() { bleManager.disconnect() }

    // 자동 재연결 토글 동작 함수
    fun toggleAutoReconnect(enabled: Boolean) {
        _uiState.update { it.copy(autoReconnect = enabled) }
        bleManager.setAutoReconnectEnabled(enabled)
    }

    fun setIntensity(level: IntensityLevel) {
        _uiState.update { it.copy(intensity = level) }
        val levelInt = when (level) {
            IntensityLevel.LOW    -> 1
            IntensityLevel.MEDIUM -> 3
            IntensityLevel.HIGH   -> 5
        }
        bleManager.sendSetIntensity(levelInt)
    }

    fun toggleDetectionMode() {
        val nextMode = if (_uiState.value.detectionMode == DetectionMode.DETECTION) DetectionMode.GENERAL else DetectionMode.DETECTION
        _uiState.update { it.copy(detectionMode = nextMode) }
        bleManager.sendSetMode(if (nextMode == DetectionMode.DETECTION) 0x02.toByte() else 0x01.toByte())
    }
}