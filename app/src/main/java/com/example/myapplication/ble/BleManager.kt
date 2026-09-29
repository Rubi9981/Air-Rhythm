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
// ============================================================================
object CommandIds {
    const val START: Byte = 0x01
    const val STOP: Byte = 0x02
    const val CALIBRATE: Byte = 0x04
    const val SET_PERIOD: Byte = 0x05
    const val SET_INTENSITY: Byte = 0x06  // 강도 설정
    const val SET_MODE: Byte = 0x07       // 인식/자율 모드 설정
}

// ============================================================================
// [장치 상태 열거형]
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
// [BleManager 싱글톤]
// ============================================================================
@SuppressLint("MissingPermission")
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

    // 코루틴 스코프 (자동 재연결 및 비동기 작업용)
    private val managerScope = CoroutineScope(Dispatchers.Main + SupervisorJob())

    // 외부 관찰용 StateFlow
    private val _connectionState = MutableStateFlow(BleConnectionState.DISCONNECTED)
    val connectionState: StateFlow<BleConnectionState> = _connectionState.asStateFlow()

    private val _telemetryData = MutableStateFlow(TelemetryData())
    val telemetryData: StateFlow<TelemetryData> = _telemetryData.asStateFlow()

    private val _scannedDevices = MutableStateFlow<List<ScannedDevice>>(emptyList())
    val scannedDevices: StateFlow<List<ScannedDevice>> = _scannedDevices.asStateFlow()

    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()

    // 내부 BLE 객체
    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothManager.adapter
    }

    private var bluetoothGatt: BluetoothGatt? = null
    private var rxCharacteristic: BluetoothGattCharacteristic? = null
    private var bleScanner: BluetoothLeScanner? = null

    // 자동 재연결 관련 변수
    private var lastConnectedDevice: BluetoothDevice? = null
    private var isExplicitDisconnect = false
    private var isAutoReconnectEnabled = true
    private var autoReconnectJob: Job? = null
    private var reconnectScanCallback: ScanCallback? = null
    private var scanTimeoutJob: Job? = null

    fun setAutoReconnectEnabled(enabled: Boolean) {
        isAutoReconnectEnabled = enabled
        Log.i(TAG, "자동 재연결 설정 변경: $enabled")
        if (!enabled) {
            cancelAutoReconnect()
        }
    }

    fun isAutoReconnectEnabled(): Boolean = isAutoReconnectEnabled

    // ========================================================================
    // [스캔 관련]
    // ========================================================================

    /**
     * BLE 스캔 시작 (다시 스캔 지원 및 12초 자동 타임아웃)
     */
    fun startScan() {
        try {
            if (!isBluetoothEnabled()) {
                Log.e(TAG, "블루투스가 꺼져 있어 스캔을 시작할 수 없습니다.")
                return
            }

            // 이미 스캔 중이면 기존 스캔 정지 후 다시 스캔
            if (_isScanning.value) {
                stopScan()
            }

            val scanner = bluetoothAdapter?.bluetoothLeScanner ?: run {
                Log.e(TAG, "BLE 스캐너를 가져올 수 없음 (블루투스 OFF?)")
                return
            }

            bleScanner = scanner
            _scannedDevices.value = emptyList()
            _isScanning.value = true
            _connectionState.value = BleConnectionState.SCANNING

            // 1. Service UUID 필터
            val filterUuid = ScanFilter.Builder()
                .setServiceUuid(ParcelUuid(BleUuids.SERVICE_UUID))
                .build()
            // 2. 기기명 필터 (Scan response 고려)
            val filterName = ScanFilter.Builder()
                .setDeviceName("Air-Rhythm")
                .build()

            val settings = ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build()

            scanner.startScan(listOf(filterUuid, filterName), settings, scanCallback)
            Log.i(TAG, "BLE 스캔 시작")

            // 12초 후 자동 스캔 완료 (배터리 절약 및 다시 스캔 활성화)
            scanTimeoutJob?.cancel()
            scanTimeoutJob = managerScope.launch {
                delay(12000L)
                if (_isScanning.value) {
                    Log.i(TAG, "BLE 스캔 타임아웃 완료")
                    stopScan()
                }
            }
        } catch (e: SecurityException) {
            Log.e(TAG, "BLE 스캔 권한이 없습니다.", e)
            _isScanning.value = false
            _connectionState.value = BleConnectionState.DISCONNECTED
        } catch (e: Exception) {
            Log.e(TAG, "BLE 스캔 시작 중 예외 발생", e)
            _isScanning.value = false
            _connectionState.value = BleConnectionState.DISCONNECTED
        }
    }

    /**
     * BLE 스캔 정지
     */
    fun stopScan() {
        try {
            scanTimeoutJob?.cancel()
            scanTimeoutJob = null

            if (_isScanning.value) {
                bleScanner?.stopScan(scanCallback)
                _isScanning.value = false
                if (_connectionState.value == BleConnectionState.SCANNING) {
                    _connectionState.value = BleConnectionState.DISCONNECTED
                }
                Log.i(TAG, "BLE 스캔 정지")
            }
        } catch (e: Exception) {
            Log.e(TAG, "BLE 스캔 정지 중 예외 발생", e)
        }
    }

    /** 스캔 콜백 */
    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val scanRecordName = result.scanRecord?.deviceName
            val name = if (!scanRecordName.isNullOrBlank()) scanRecordName
                       else if (!device.name.isNullOrBlank()) device.name
                       else "Air-Rhythm"
            val address = device.address
            val rssi = result.rssi

            val currentList = _scannedDevices.value.toMutableList()
            val existingIndex = currentList.indexOfFirst { it.address.equals(address, ignoreCase = true) }
            if (existingIndex >= 0) {
                currentList[existingIndex] = ScannedDevice(name, address, rssi, device)
            } else {
                currentList.add(ScannedDevice(name, address, rssi, device))
            }
            _scannedDevices.value = currentList
            Log.i(TAG, "디바이스 발견: $name ($address) RSSI=$rssi")
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

    fun connectToDevice(device: BluetoothDevice) {
        stopScan()
        cancelAutoReconnect()
        isExplicitDisconnect = false
        lastConnectedDevice = device

        _connectionState.value = BleConnectionState.CONNECTING
        Log.i(TAG, "연결 시도: ${device.name} (${device.address})")

        bluetoothGatt?.close()
        bluetoothGatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    fun connectToDevice(scannedDevice: ScannedDevice) {
        connectToDevice(scannedDevice.device)
    }

    fun disconnect() {
        isExplicitDisconnect = true
        cancelAutoReconnect()

        _connectionState.value = BleConnectionState.DISCONNECTING
        bluetoothGatt?.let { gatt ->
            gatt.disconnect()
        } ?: run {
            _connectionState.value = BleConnectionState.DISCONNECTED
        }
    }

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
     * ★ 자동 재연결 프로세스
     */
    private fun startAutoReconnect() {
        if (!isAutoReconnectEnabled) {
            Log.i(TAG, "자동 재연결이 비활성화되어 재연결을 시도하지 않습니다.")
            _connectionState.value = BleConnectionState.DISCONNECTED
            return
        }

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

    /** GATT 콜백 */
    private val gattCallback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Log.i(TAG, "GATT 연결 성공! 서비스 디스커버리 시작...")
                    cancelAutoReconnect()
                    gatt.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    Log.i(TAG, "GATT 연결 해제됨 (status=$status, isExplicit=$isExplicitDisconnect)")
                    gatt.close()
                    bluetoothGatt = null
                    rxCharacteristic = null
                    _telemetryData.value = TelemetryData()

                    if (!isExplicitDisconnect && isAutoReconnectEnabled && lastConnectedDevice != null) {
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

            val service = gatt.getService(BleUuids.SERVICE_UUID)
            if (service == null) {
                Log.e(TAG, "RespiSync 서비스를 찾을 수 없음! UUID 불일치 확인 필요")
                disconnect()
                return
            }

            rxCharacteristic = service.getCharacteristic(BleUuids.CHARACTERISTIC_RX)
            if (rxCharacteristic == null) {
                Log.e(TAG, "RX 캐릭터리스틱을 찾을 수 없음!")
            }

            val txCharacteristic = service.getCharacteristic(BleUuids.CHARACTERISTIC_TX)
            if (txCharacteristic != null) {
                gatt.setCharacteristicNotification(txCharacteristic, true)

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

    /** 타격 시작 (주기 지정) */
    fun sendStart(periodMs: Int = 500) = sendCommand(CommandIds.START, mode = 0x01, periodMs = periodMs)

    /** 정상 정지 */
    fun sendStop() = sendCommand(CommandIds.STOP, mode = 0x00, periodMs = 0)

    /** 캘리브레이션 모드 진입 */
    fun sendCalibrate() = sendCommand(CommandIds.CALIBRATE, mode = 0x00, periodMs = 0)

    /** 타격 주기 변경 */
    fun sendSetPeriod(periodMs: Int) = sendCommand(CommandIds.SET_PERIOD, mode = 0x01, periodMs = periodMs)

    /** 모드 변경 전송 */
    fun sendSetMode(mode: Byte): Boolean = sendCommand(CommandIds.SET_MODE, mode = mode, periodMs = 0)

    /** 강도 변경 전송 */
    fun sendSetIntensity(level: Int): Boolean = sendCommand(CommandIds.SET_INTENSITY, mode = level.toByte(), periodMs = 0)

    // ========================================================================
    // [텔레메트리 패킷 파서 - 12바이트]
    // ========================================================================

    private fun parseTelemetryPacket(data: ByteArray) {
        if (data[0] != 0x55.toByte() || data[1] != 0xAA.toByte()) {
            Log.w(TAG, "텔레메트리 헤더 불일치!")
            return
        }

        var checksum: Byte = 0
        for (i in 2..10) {
            checksum = (checksum.toInt() xor data[i].toInt()).toByte()
        }
        if (checksum != data[11]) {
            Log.w(TAG, "텔레메트리 체크섬 오류!")
            return
        }

        val deviceState = DeviceState.fromCode(data[2].toInt() and 0xFF)
        val chestPressure = (data[3].toInt() and 0xFF) or ((data[4].toInt() and 0xFF) shl 8)
        val signedPressure = if (chestPressure > 32767) chestPressure - 65536 else chestPressure
        val respirationPhase = RespirationPhase.fromCode(data[5].toInt() and 0xFF)
        val motorActive = (data[6].toInt() and 0xFF) == 0x01
        val powerStatus = data[7].toInt() and 0xFF
        val currentPeriod = (data[8].toInt() and 0xFF) or ((data[9].toInt() and 0xFF) shl 8)
        val errorCode = data[10].toInt() and 0xFF

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

    fun isConnected(): Boolean = _connectionState.value == BleConnectionState.CONNECTED

    fun isBluetoothEnabled(): Boolean = bluetoothAdapter?.isEnabled == true

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