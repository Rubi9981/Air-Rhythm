package com.example.myapplication.ble

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.os.ParcelUuid
import android.util.Log
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.UUID

/**
 * ============================================================================
 * [RespiSync BLE Manager] - ESP32와의 BLE 통신을 전담하는 싱글톤 매니저
 * ============================================================================
 *
 * 역할:
 *   - BLE 스캔 (ESP32 디바이스 검색)
 *   - GATT 연결/해제 관리
 *   - 예기치 않은 연결 끊김(전원 OFF/거리 이탈) 시 자동 재연결(Auto-Reconnect)
 *   - 8바이트 명령 패킷 빌더 + Write
 *   - 12바이트 텔레메트리 패킷 파서 + Notify 수신
 *
 * 사용법:
 *   val bleManager = BleManager.getInstance(context)
 *   bleManager.startScan()          // 스캔 시작
 *   bleManager.connectToDevice(device)  // 연결
 *   bleManager.sendStart(periodMs)  // 타격 시작 명령
 *   bleManager.sendStop()           // 정지 명령
 *
 * 주의: 이 클래스를 사용하려면 AndroidManifest.xml에 BLE 권한이 필요합니다.
 *    (BLUETOOTH_SCAN, BLUETOOTH_CONNECT, ACCESS_FINE_LOCATION)
 * ============================================================================
 */

// ============================================================================
// [BLE UUID] - ESP32 펌웨어와 반드시 동일해야 합니다!
// ============================================================================
object BleUuids {
    val SERVICE_UUID: UUID = UUID.fromString("4fafc201-1fb5-459e-8fcc-c5c9c331914b")
    val CHARACTERISTIC_RX: UUID = UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a8")  // 앱→ESP32 (Write)
    val CHARACTERISTIC_TX: UUID = UUID.fromString("ba000001-36e1-4688-b7f5-ea07361b26a8")  // ESP32→앱 (Notify)

    // CCCD UUID (Notify 활성화용 - BLE 표준)
    val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
}

// ============================================================================
// [명령 ID] - ESP32 펌웨어의 CMD 정의와 일치
// 새 명령 추가 시: 여기에 상수 추가 → sendCommand() 호출하는 함수 추가
// ============================================================================
object CommandIds {
    const val START: Byte = 0x01
    const val STOP: Byte = 0x02
    const val CALIBRATE: Byte = 0x04
    const val SET_PERIOD: Byte = 0x05
    // [TODO] 새 명령 추가 예시:
    // const val SET_INTENSITY: Byte = 0x06
    // const val ZONE_SELECT: Byte = 0x07
}

// ============================================================================
// [장치 상태 열거형] - 텔레메트리에서 수신되는 상태값
// ============================================================================
enum class DeviceState(val code: Int) {
    IDLE(0x00),
    RUNNING(0x01),
    CALIBRATING(0x02),
    ERROR(0xFF);

    companion object {
        fun fromCode(code: Int): DeviceState = entries.find { it.code == code } ?: ERROR
    }
}

// ============================================================================
// [호흡 위상 열거형]
// ============================================================================
enum class RespirationPhase(val code: Int) {
    NONE(0x00),
    INHALE(0x01),  // 흡기 (들숨)
    EXHALE(0x02);  // 호기 (날숨)

    companion object {
        fun fromCode(code: Int): RespirationPhase = entries.find { it.code == code } ?: NONE
    }
}

// ============================================================================
// [텔레메트리 데이터 클래스] - 12바이트 패킷 파싱 결과
// ============================================================================
data class TelemetryData(
    val deviceState: DeviceState = DeviceState.IDLE,
    val chestPressure: Int = 0,           // 흉부 센서 호흡 신호값 (필터링 후, signed 16-bit)
    val respirationPhase: RespirationPhase = RespirationPhase.NONE,
    val motorActive: Boolean = false,      // 모터 현재 ON 여부
    val powerStatus: Int = 100,            // 전원 상태 (유선=100%)
    val currentPeriodMs: Int = 500,        // 현재 타격 주기
    val errorCode: Int = 0                 // 에러 코드
)

// ============================================================================
// [BLE 연결 상태]
// ============================================================================
enum class BleConnectionState {
    DISCONNECTED,  // 미연결
    SCANNING,      // 스캔 중
    CONNECTING,    // 연결 시도 중
    CONNECTED,     // 연결 완료 (서비스 디스커버리 완료)
    RECONNECTING,  // 자동 재연결 시도 중 (ESP32 전원 재부팅 감지 대기)
    DISCONNECTING  // 연결 해제 중
}

// ============================================================================
// [스캔 결과 데이터 클래스]
// ============================================================================
data class ScannedDevice(
    val name: String,
    val address: String,
    val rssi: Int,
    val device: BluetoothDevice
)

// ============================================================================
// [BleManager 싱글톤] - 앱 전체에서 하나의 인스턴스로 BLE 관리
// ============================================================================
@SuppressLint("MissingPermission")  // 권한 체크는 UI 레이어에서 수행
class BleManager private constructor(private val context: Context) {

    companion object {
        private const val TAG = "BleManager"

        @Volatile
        private var instance: BleManager? = null

        fun getInstance(context: Context): BleManager {
            return instance ?: synchronized(this) {
                instance ?: BleManager(context.applicationContext).also { instance = it }
            }
        }
    }

    // --- 코루틴 스코프 (자동 재연결 및 비동기 작업용) ---
    private val managerScope = CoroutineScope(Dispatchers.Main + SupervisorJob())

    // --- 외부 관찰용 StateFlow ---
    private val _connectionState = MutableStateFlow(BleConnectionState.DISCONNECTED)
    val connectionState: StateFlow<BleConnectionState> = _connectionState.asStateFlow()

    private val _telemetryData = MutableStateFlow(TelemetryData())
    val telemetryData: StateFlow<TelemetryData> = _telemetryData.asStateFlow()

    private val _scannedDevices = MutableStateFlow<List<ScannedDevice>>(emptyList())
    val scannedDevices: StateFlow<List<ScannedDevice>> = _scannedDevices.asStateFlow()

    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()

    // --- 내부 BLE 객체 ---
    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothManager.adapter
    }

    private var bluetoothGatt: BluetoothGatt? = null
    private var rxCharacteristic: BluetoothGattCharacteristic? = null
    private var bleScanner: BluetoothLeScanner? = null

    // --- 자동 재연결 관련 변수 ---
    private var lastConnectedDevice: BluetoothDevice? = null
    private var isExplicitDisconnect = false
    private var autoReconnectJob: Job? = null
    private var reconnectScanCallback: ScanCallback? = null

    // ========================================================================
    // [스캔 관련]
    // ========================================================================

    /**
     * BLE 스캔 시작
     * - ESP32의 Service UUID로 필터링하여 우리 디바이스만 검색
     * - 10초 후 자동 정지 (배터리 절약)
     */
    fun startScan() {
        if (_isScanning.value) return

        val scanner = bluetoothAdapter?.bluetoothLeScanner ?: run {
            Log.e(TAG, "BLE 스캐너를 가져올 수 없음 (블루투스 OFF?)")
            return
        }

        bleScanner = scanner
        _scannedDevices.value = emptyList()
        _isScanning.value = true
        _connectionState.value = BleConnectionState.SCANNING

        // Service UUID로 필터링 (우리 ESP32만 찾기)
        val filter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(BleUuids.SERVICE_UUID))
            .build()

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)  // 빠른 검색 (전력 소비 높음)
            .build()

        scanner.startScan(listOf(filter), settings, scanCallback)
        Log.i(TAG, "BLE 스캔 시작 (Service UUID 필터)")
        // [TODO] 10초 타임아웃 후 자동 정지하려면 Handler.postDelayed 사용
    }

    /**
     * BLE 스캔 정지
     */
    fun stopScan() {
        if (!_isScanning.value) return

        bleScanner?.stopScan(scanCallback)
        _isScanning.value = false
        if (_connectionState.value == BleConnectionState.SCANNING) {
            _connectionState.value = BleConnectionState.DISCONNECTED
        }
        Log.i(TAG, "BLE 스캔 정지")
    }

    /** 스캔 콜백 - 디바이스 발견 시 호출됨 */
    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val name = device.name ?: "Unknown"
            val address = device.address
            val rssi = result.rssi

            // 중복 방지
            val currentList = _scannedDevices.value.toMutableList()
            if (currentList.none { it.address == address }) {
                currentList.add(ScannedDevice(name, address, rssi, device))
                _scannedDevices.value = currentList
                Log.i(TAG, "디바이스 발견: $name ($address) RSSI=$rssi")
            }
        }

        override fun onScanFailed(errorCode: Int) {
            Log.e(TAG, "스캔 실패! 에러 코드: $errorCode")
            _isScanning.value = false
            _connectionState.value = BleConnectionState.DISCONNECTED
        }
    }

    // ========================================================================
    // [연결 관련]
    // ========================================================================

    /**
     * 특정 디바이스에 GATT 연결
     * @param device 스캔에서 찾은 BluetoothDevice
     */
    fun connectToDevice(device: BluetoothDevice) {
        stopScan()  // 연결 시도 전 스캔 정지
        cancelAutoReconnect() // 진행 중인 재연결 중단
        isExplicitDisconnect = false
        lastConnectedDevice = device

        _connectionState.value = BleConnectionState.CONNECTING
        Log.i(TAG, "연결 시도: ${device.name} (${device.address})")

        // 기존 연결 안전하게 정리 후 재연결
        bluetoothGatt?.close()
        bluetoothGatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    /**
     * 연결된 ScannedDevice로 연결 (편의 메서드)
     */
    fun connectToDevice(scannedDevice: ScannedDevice) {
        connectToDevice(scannedDevice.device)
    }

    /**
     * GATT 연결 해제 (사용자가 직접 명시적으로 해제할 때 호출)
     */
    fun disconnect() {
        isExplicitDisconnect = true
        cancelAutoReconnect()

        _connectionState.value = BleConnectionState.DISCONNECTING
        bluetoothGatt?.let { gatt ->
            gatt.disconnect()
            // close()는 onConnectionStateChange 콜백에서 호출
        } ?: run {
            _connectionState.value = BleConnectionState.DISCONNECTED
        }
    }

    /**
     * 자동 재연결 프로세스 취소
     */
    fun cancelAutoReconnect() {
        autoReconnectJob?.cancel()
        autoReconnectJob = null
        reconnectScanCallback?.let { callback ->
            try {
                bluetoothAdapter?.bluetoothLeScanner?.stopScan(callback)
            } catch (e: Exception) {
                Log.w(TAG, "재연결 스캔 중지 예외: ${e.message}")
            }
        }
        reconnectScanCallback = null
    }

    /**
     * ★ 자동 재연결 시작 (ESP32 전원 꺼짐 등으로 예기치 않게 끊겼을 때)
     * - 타겟 디바이스(MAC 주소)를 필터링하여 스캔을 즉시 시작
     * - ESP32 전원이 다시 켜져 부팅 광고(Advertising)를 시작하면 1~2초 내 즉시 감지하여 자동 재연결 수행
     */
    private fun startAutoReconnect() {
        val target = lastConnectedDevice ?: run {
            _connectionState.value = BleConnectionState.DISCONNECTED
            return
        }

        cancelAutoReconnect()
        _connectionState.value = BleConnectionState.RECONNECTING
        Log.i(TAG, "★ ESP32 전원 꺼짐/신호 끊김 감지 -> 자동 재연결 대기 시작: ${target.address}")

        autoReconnectJob = managerScope.launch {
            val scanner = bluetoothAdapter?.bluetoothLeScanner
            if (scanner == null || !isBluetoothEnabled()) {
                Log.w(TAG, "블루투스가 꺼져 있어 자동 재연결 불가")
                _connectionState.value = BleConnectionState.DISCONNECTED
                return@launch
            }

            val filter = ScanFilter.Builder()
                .setDeviceAddress(target.address)
                .build()

            val settings = ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build()

            val callback = object : ScanCallback() {
                override fun onScanResult(callbackType: Int, result: ScanResult) {
                    if (result.device.address.equals(target.address, ignoreCase = true)) {
                        Log.i(TAG, "★ ESP32 재부팅 감지! 즉시 재연결 수행: ${result.device.address}")
                        cancelAutoReconnect()
                        connectToDevice(result.device)
                    }
                }

                override fun onScanFailed(errorCode: Int) {
                    Log.e(TAG, "자동 재연결 스캔 실패: errorCode=$errorCode")
                }
            }

            reconnectScanCallback = callback

            try {
                scanner.startScan(listOf(filter), settings, callback)
                Log.i(TAG, "ESP32 광고 비콘 대기 중...")

                // 취소될 때까지 코루틴 유지
                while (isActive) {
                    delay(3000)
                }
            } catch (e: Exception) {
                Log.e(TAG, "자동 재연결 스캔 예외 발생", e)
            } finally {
                try {
                    scanner.stopScan(callback)
                } catch (_: Exception) {}
            }
        }
    }

    /** GATT 콜백 - 연결 상태 변경, 서비스 발견, 데이터 수신 처리 */
    private val gattCallback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Log.i(TAG, "GATT 연결 성공! 서비스 디스커버리 시작...")
                    cancelAutoReconnect()
                    gatt.discoverServices()  // 연결 직후 서비스 탐색 필수
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    Log.i(TAG, "GATT 연결 해제됨 (status=$status, isExplicit=$isExplicitDisconnect)")
                    gatt.close()
                    bluetoothGatt = null
                    rxCharacteristic = null
                    _telemetryData.value = TelemetryData()  // 텔레메트리 초기화

                    // 사용자가 명시적으로 끊은 게 아니고 이전 연결 기기가 있다면 -> 자동 재연결 시작
                    if (!isExplicitDisconnect && lastConnectedDevice != null) {
                        startAutoReconnect()
                    } else {
                        _connectionState.value = BleConnectionState.DISCONNECTED
                    }
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(TAG, "서비스 디스커버리 실패: status=$status")
                disconnect()
                return
            }

            Log.i(TAG, "서비스 디스커버리 완료!")

            // 우리 서비스 찾기
            val service = gatt.getService(BleUuids.SERVICE_UUID)
            if (service == null) {
                Log.e(TAG, "RespiSync 서비스를 찾을 수 없음! UUID 불일치 확인 필요")
                disconnect()
                return
            }

            // RX 캐릭터리스틱 (앱 → ESP32, Write)
            rxCharacteristic = service.getCharacteristic(BleUuids.CHARACTERISTIC_RX)
            if (rxCharacteristic == null) {
                Log.e(TAG, "RX 캐릭터리스틱을 찾을 수 없음!")
            }

            // TX 캐릭터리스틱 (ESP32 → 앱, Notify)
            val txCharacteristic = service.getCharacteristic(BleUuids.CHARACTERISTIC_TX)
            if (txCharacteristic != null) {
                // Notify 활성화: 로컬 설정
                gatt.setCharacteristicNotification(txCharacteristic, true)

                // CCCD에 Notify 활성화 값 쓰기 (ESP32에게 알림)
                val descriptor = txCharacteristic.getDescriptor(BleUuids.CCCD_UUID)
                if (descriptor != null) {
                    descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    gatt.writeDescriptor(descriptor)
                    Log.i(TAG, "TX Notify 활성화 완료")
                }
            } else {
                Log.e(TAG, "TX 캐릭터리스틱을 찾을 수 없음!")
            }

            cancelAutoReconnect()
            _connectionState.value = BleConnectionState.CONNECTED
            Log.i(TAG, "★ BLE 연결 완료 - 명령 전송/텔레메트리 수신 가능")
        }

        /**
         * Notify 데이터 수신 콜백 - ESP32에서 12바이트 텔레메트리가 올 때마다 호출됨
         */
        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (characteristic.uuid == BleUuids.CHARACTERISTIC_TX) {
                val data = characteristic.value
                if (data != null && data.size == 12) {
                    parseTelemetryPacket(data)
                }
            }
        }
    }

    // ========================================================================
    // [명령 전송 - 8바이트 패킷 빌더]
    // ========================================================================

    /**
     * 범용 명령 패킷 빌더 + 전송
     *
     * 패킷 구조 (8바이트 고정):
     *   [0] 0xAA (Header 1)
     *   [1] 0x55 (Header 2)
     *   [2] Command ID
     *   [3] Mode (0x00=Standby, 0x01=Autonomous)
     *   [4] Period Low Byte (Little Endian)
     *   [5] Period High Byte (Little Endian)
     *   [6] Reserved (0x00)
     *   [7] Checksum (Byte[2]^[3]^[4]^[5]^[6])
     *
     * ★ 새 명령 추가 시 이 함수를 호출하는 wrapper 함수를 아래에 추가하면 됩니다.
     */
    private fun sendCommand(commandId: Byte, mode: Byte = 0x01, periodMs: Int = 0): Boolean {
        val rxChar = rxCharacteristic ?: run {
            Log.e(TAG, "RX 캐릭터리스틱 없음 (연결 안됨?)")
            return false
        }
        val gatt = bluetoothGatt ?: return false

        val pkt = ByteArray(8)
        pkt[0] = 0xAA.toByte()  // Header 1
        pkt[1] = 0x55           // Header 2
        pkt[2] = commandId
        pkt[3] = mode
        pkt[4] = (periodMs and 0xFF).toByte()         // Period Low (Little Endian)
        pkt[5] = ((periodMs shr 8) and 0xFF).toByte() // Period High (Little Endian)
        pkt[6] = 0x00  // Reserved

        // Checksum: Byte[2] ^ [3] ^ [4] ^ [5] ^ [6]
        pkt[7] = (pkt[2].toInt() xor pkt[3].toInt() xor pkt[4].toInt()
                xor pkt[5].toInt() xor pkt[6].toInt()).toByte()

        rxChar.value = pkt
        rxChar.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        val success = gatt.writeCharacteristic(rxChar)

        Log.i(TAG, "명령 전송 [CMD=0x${String.format("%02X", commandId)}] " +
                "Period=${periodMs}ms → ${if (success) "성공" else "실패"}")
        return success
    }

    // --- 명령별 편의 함수들 ---

    /** 타격 시작 (주기 지정) */
    fun sendStart(periodMs: Int = 500) = sendCommand(CommandIds.START, mode = 0x01, periodMs = periodMs)

    /** 정상 정지 */
    fun sendStop() = sendCommand(CommandIds.STOP, mode = 0x00, periodMs = 0)

    /** 캘리브레이션 모드 진입 */
    fun sendCalibrate() = sendCommand(CommandIds.CALIBRATE, mode = 0x00, periodMs = 0)

    /** 타격 주기 변경 (동작 중에도 실시간 적용) */
    fun sendSetPeriod(periodMs: Int) = sendCommand(CommandIds.SET_PERIOD, mode = 0x01, periodMs = periodMs)

    // =========================================================================
    // [TODO] 새 명령 추가 가이드:
    // =========================================================================
    // 1. CommandIds 객체에 상수 추가: const val MY_CMD: Byte = 0x06
    // 2. 여기에 편의 함수 추가:
    //    fun sendMyCommand(param: Int) = sendCommand(CommandIds.MY_CMD, mode = ..., periodMs = param)
    // 3. ESP32 펌웨어의 handleCommand()에 case 추가
    // 4. UI에서 bleManager.sendMyCommand(값) 호출
    //
    // 예시 - 강도 조절 명령:
    //   fun sendSetIntensity(level: Int) = sendCommand(CommandIds.SET_INTENSITY, mode = level.toByte(), periodMs = 0)
    // =========================================================================

    // ========================================================================
    // [텔레메트리 패킷 파서 - 12바이트]
    // ========================================================================

    /**
     * ESP32에서 수신한 12바이트 텔레메트리 패킷 파싱
     *
     * 패킷 구조:
     *   [0]  0x55 (Header 1)
     *   [1]  0xAA (Header 2)
     *   [2]  Device State
     *   [3]  Chest Pressure Low (Little Endian, signed)
     *   [4]  Chest Pressure High
     *   [5]  Respiration Phase
     *   [6]  Motor Active (0x00=OFF, 0x01=ON)
     *   [7]  Power Status (0x64 = 100%)
     *   [8]  Current Period Low (Little Endian)
     *   [9]  Current Period High
     *   [10] Error Code
     *   [11] Checksum (Byte[2]~[10] XOR)
     */
    private fun parseTelemetryPacket(data: ByteArray) {
        // 헤더 검증
        if (data[0] != 0x55.toByte() || data[1] != 0xAA.toByte()) {
            Log.w(TAG, "텔레메트리 헤더 불일치!")
            return
        }

        // 체크섬 검증: Byte[2] ~ Byte[10] XOR
        var checksum: Byte = 0
        for (i in 2..10) {
            checksum = (checksum.toInt() xor data[i].toInt()).toByte()
        }
        if (checksum != data[11]) {
            Log.w(TAG, "텔레메트리 체크섬 오류!")
            return
        }

        // 파싱
        val deviceState = DeviceState.fromCode(data[2].toInt() and 0xFF)
        val chestPressure = (data[3].toInt() and 0xFF) or ((data[4].toInt() and 0xFF) shl 8)
        // signed 16-bit 변환
        val signedPressure = if (chestPressure > 32767) chestPressure - 65536 else chestPressure
        val respirationPhase = RespirationPhase.fromCode(data[5].toInt() and 0xFF)
        val motorActive = (data[6].toInt() and 0xFF) == 0x01
        val powerStatus = data[7].toInt() and 0xFF
        val currentPeriod = (data[8].toInt() and 0xFF) or ((data[9].toInt() and 0xFF) shl 8)
        val errorCode = data[10].toInt() and 0xFF

        // StateFlow 업데이트 (UI에서 collect하여 실시간 표시)
        _telemetryData.value = TelemetryData(
            deviceState = deviceState,
            chestPressure = signedPressure,
            respirationPhase = respirationPhase,
            motorActive = motorActive,
            powerStatus = powerStatus,
            currentPeriodMs = currentPeriod,
            errorCode = errorCode
        )
    }

    // ========================================================================
    // [유틸리티]
    // ========================================================================

    /** 현재 BLE가 연결 상태인지 확인 */
    fun isConnected(): Boolean = _connectionState.value == BleConnectionState.CONNECTED

    /** 블루투스 사용 가능 여부 */
    fun isBluetoothEnabled(): Boolean = bluetoothAdapter?.isEnabled == true

    /** 리소스 정리 (Activity onDestroy 시 호출) */
    fun cleanup() {
        isExplicitDisconnect = true
        cancelAutoReconnect()
        stopScan()
        bluetoothGatt?.let { gatt ->
            gatt.disconnect()
            gatt.close()
        }
        bluetoothGatt = null
        rxCharacteristic = null
        _connectionState.value = BleConnectionState.DISCONNECTED
    }
}
