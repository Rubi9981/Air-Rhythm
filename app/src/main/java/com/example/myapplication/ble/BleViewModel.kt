package com.example.myapplication.ble

import android.app.Application
import android.bluetooth.BluetoothDevice
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.stateIn

/**
 * ============================================================================
 * [BLE ViewModel] - UI와 BleManager 사이의 중간 레이어
 * ============================================================================
 *
 * 역할:
 *   - BleManager의 StateFlow를 UI에 노출
 *   - UI 이벤트를 BleManager 메서드 호출로 변환
 *   - ViewModel 생명주기와 BLE 리소스 관리 연동
 *
 * 사용법 (Composable에서):
 *   val viewModel: BleViewModel = viewModel()
 *   val connectionState by viewModel.connectionState.collectAsState()
 *   val telemetry by viewModel.telemetryData.collectAsState()
 *
 * ============================================================================
 */
class BleViewModel(application: Application) : AndroidViewModel(application) {

    private val bleManager = BleManager.getInstance(application)

    // --- UI에 노출되는 상태 ---
    val connectionState: StateFlow<BleConnectionState> = bleManager.connectionState
    val telemetryData: StateFlow<TelemetryData> = bleManager.telemetryData
    val scannedDevices: StateFlow<List<ScannedDevice>> = bleManager.scannedDevices
    val isScanning: StateFlow<Boolean> = bleManager.isScanning

    // --- 스캔 ---
    fun startScan() = bleManager.startScan()
    fun stopScan() = bleManager.stopScan()

    // --- 연결 ---
    fun connectToDevice(device: ScannedDevice) = bleManager.connectToDevice(device)
    fun disconnect() = bleManager.disconnect()

    // --- 명령 전송 ---
    /** 타격 시작 (주기: ms 단위, 예: 500 = 0.5초 간격) */
    fun sendStart(periodMs: Int = 500) = bleManager.sendStart(periodMs)

    /** 정상 정지 */
    fun sendStop() = bleManager.sendStop()

    /** 캘리브레이션 */
    fun sendCalibrate() = bleManager.sendCalibrate()

    /** 타격 주기 실시간 변경 */
    fun sendSetPeriod(periodMs: Int) = bleManager.sendSetPeriod(periodMs)

    // =========================================================================
    // [TODO] 새 명령 추가 시:
    //   fun sendNewCommand(param: Int) = bleManager.sendNewCommand(param)
    //   → UI에서 viewModel.sendNewCommand(값) 으로 호출
    // =========================================================================

    // --- 유틸 ---
    fun isConnected(): Boolean = bleManager.isConnected()
    fun isBluetoothEnabled(): Boolean = bleManager.isBluetoothEnabled()

    override fun onCleared() {
        super.onCleared()
        // ViewModel이 파괴될 때 BLE 리소스 정리
        bleManager.cleanup()
    }
}
