package com.example.myapplication.ui.screens

import android.Manifest
import android.os.Build
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.myapplication.ble.BleConnectionState
import com.example.myapplication.ble.BleViewModel
import com.example.myapplication.ble.ScannedDevice
import com.example.myapplication.ui.theme.MedicalBlueContainer
import com.google.accompanist.permissions.ExperimentalPermissionsApi
import com.google.accompanist.permissions.rememberMultiplePermissionsState

@OptIn(ExperimentalPermissionsApi::class)
@Composable
fun DevicePairingScreen(bleViewModel: BleViewModel = viewModel()) {

    // --- BLE 권한 요청 (Android 12+ / 이하 분기) ---
    val blePermissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        listOf(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT
        )
    } else {
        listOf(
            Manifest.permission.ACCESS_FINE_LOCATION
        )
    }
    val permissionState = rememberMultiplePermissionsState(blePermissions)

    // --- ViewModel 상태 구독 ---
    val connectionState by bleViewModel.connectionState.collectAsState()
    val scannedDevices by bleViewModel.scannedDevices.collectAsState()
    val isScanning by bleViewModel.isScanning.collectAsState()

    // --- 레이더 펄스 애니메이션 ---
    val infiniteTransition = rememberInfiniteTransition(label = "Radar")
    val scale by infiniteTransition.animateFloat(
        initialValue = 0.8f,
        targetValue = 2.0f,
        animationSpec = infiniteRepeatable(
            animation = tween(1800, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "RippleScale"
    )
    val alpha by infiniteTransition.animateFloat(
        initialValue = 0.5f,
        targetValue = 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(1800, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "RippleAlpha"
    )

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // --- 연결 상태 배너 ---
        when (connectionState) {
            BleConnectionState.CONNECTED -> {
                Surface(
                    color = Color(0xFF4CAF50).copy(alpha = 0.15f),
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Row(
                        modifier = Modifier.padding(12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(Icons.Default.CheckCircle, null, tint = Color(0xFF4CAF50), modifier = Modifier.size(20.dp))
                        Spacer(Modifier.width(8.dp))
                        Text("ESP32 연결됨", fontWeight = FontWeight.Bold, fontSize = 14.sp, color = Color(0xFF4CAF50))
                    }
                }
                Spacer(Modifier.height(8.dp))
            }
            BleConnectionState.CONNECTING -> {
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(4.dp))
                Text("연결 중...", fontSize = 12.sp, color = Color.Gray)
                Spacer(Modifier.height(8.dp))
            }
            BleConnectionState.RECONNECTING -> {
                Surface(
                    color = Color(0xFFFF9800).copy(alpha = 0.15f),
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(modifier = Modifier.padding(12.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(16.dp),
                                strokeWidth = 2.dp,
                                color = Color(0xFFF57C00)
                            )
                            Spacer(Modifier.width(8.dp))
                            Text(
                                "기기 재부팅 대기 중 (자동 재연결)",
                                fontWeight = FontWeight.Bold,
                                fontSize = 13.sp,
                                color = Color(0xFFF57C00)
                            )
                        }
                        Spacer(Modifier.height(4.dp))
                        Text(
                            "ESP32 전원이 켜지면 즉시 자동으로 다시 연결됩니다.",
                            fontSize = 11.sp,
                            color = Color.DarkGray
                        )
                    }
                }
                Spacer(Modifier.height(8.dp))
            }
            else -> {}
        }

        Text(
            "Ensure your RespiSync vest device is powered on and in BLE range.",
            color = Color.Gray,
            fontSize = 13.sp
        )
        Spacer(Modifier.height(24.dp))

        // --- 레이더 펄스 중앙 아이콘 ---
        Box(contentAlignment = Alignment.Center, modifier = Modifier.size(140.dp)) {
            if (isScanning || connectionState == BleConnectionState.RECONNECTING) {
                Box(
                    modifier = Modifier
                        .size(100.dp)
                        .scale(scale)
                        .clip(CircleShape)
                        .background(
                            if (connectionState == BleConnectionState.RECONNECTING)
                                Color(0xFFFF9800).copy(alpha = alpha)
                            else MedicalBlueContainer.copy(alpha = alpha)
                        )
                )
            }
            Surface(
                shape = CircleShape,
                color = when (connectionState) {
                    BleConnectionState.CONNECTED -> Color(0xFF4CAF50)
                    BleConnectionState.RECONNECTING -> Color(0xFFFFA000)
                    else -> MedicalBlueContainer
                },
                shadowElevation = 6.dp,
                modifier = Modifier.size(64.dp)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(
                        if (connectionState == BleConnectionState.CONNECTED)
                            Icons.Default.BluetoothConnected
                        else Icons.Default.BluetoothSearching,
                        contentDescription = null,
                        tint = Color.White,
                        modifier = Modifier.size(30.dp)
                    )
                }
            }
        }
        Spacer(Modifier.height(20.dp))

        // --- 권한 미부여 시 안내 ---
        if (!permissionState.allPermissionsGranted) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = Color(0xFFFFF3E0)),
                shape = RoundedCornerShape(12.dp)
            ) {
                Column(Modifier.padding(16.dp)) {
                    Text("BLE 권한이 필요합니다", fontWeight = FontWeight.Bold, fontSize = 14.sp)
                    Spacer(Modifier.height(8.dp))
                    Text("디바이스 스캔과 연결을 위해 블루투스 권한을 허용해주세요.", fontSize = 12.sp, color = Color.Gray)
                    Spacer(Modifier.height(12.dp))
                    Button(
                        onClick = { permissionState.launchMultiplePermissionRequest() },
                        shape = RoundedCornerShape(20.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = MedicalBlueContainer)
                    ) {
                        Text("권한 허용하기")
                    }
                }
            }
            Spacer(Modifier.height(16.dp))
        }

        // --- 검색된 디바이스 목록 ---
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        "Available Devices (${scannedDevices.size})",
                        fontWeight = FontWeight.Bold,
                        fontSize = 14.sp,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    IconButton(
                        onClick = {
                            if (permissionState.allPermissionsGranted) {
                                bleViewModel.startScan()
                            } else {
                                permissionState.launchMultiplePermissionRequest()
                            }
                        },
                        modifier = Modifier.size(24.dp)
                    ) {
                        Icon(
                            Icons.Default.Refresh,
                            contentDescription = "Refresh scan",
                            tint = MedicalBlueContainer
                        )
                    }
                }
                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)

                if (scannedDevices.isEmpty()) {
                    // 디바이스 미발견 시 안내
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(32.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        if (isScanning) {
                            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                                CircularProgressIndicator(
                                    modifier = Modifier.size(24.dp),
                                    strokeWidth = 2.dp,
                                    color = MedicalBlueContainer
                                )
                                Spacer(Modifier.height(8.dp))
                                Text("스캔 중...", fontSize = 12.sp, color = Color.Gray)
                            }
                        } else {
                            Text(
                                "디바이스를 찾으려면 스캔을 시작하세요",
                                fontSize = 13.sp,
                                color = Color.Gray
                            )
                        }
                    }
                } else {
                    // 발견된 디바이스 리스트
                    scannedDevices.forEach { device ->
                        DeviceRow(
                            device = device,
                            isConnecting = connectionState == BleConnectionState.CONNECTING || connectionState == BleConnectionState.RECONNECTING,
                            isConnected = connectionState == BleConnectionState.CONNECTED,
                            onConnectClick = { bleViewModel.connectToDevice(device) }
                        )
                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
                    }
                }
            }
        }

        Spacer(Modifier.weight(1f))

        // --- 연결 해제 / 재연결 취소 버튼 (연결 중이거나 재연결 중일 때 표시) ---
        if (connectionState == BleConnectionState.CONNECTED || connectionState == BleConnectionState.RECONNECTING) {
            OutlinedButton(
                onClick = { bleViewModel.disconnect() },
                modifier = Modifier
                    .fillMaxWidth()
                    .height(48.dp),
                shape = RoundedCornerShape(24.dp),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = Color(0xFFE53935))
            ) {
                Icon(Icons.Default.LinkOff, contentDescription = null)
                Spacer(Modifier.width(8.dp))
                Text(
                    if (connectionState == BleConnectionState.RECONNECTING) "자동 재연결 취소" else "연결 해제",
                    fontWeight = FontWeight.Bold
                )
            }
            Spacer(Modifier.height(12.dp))
        }

        // --- 스캔 시작/정지 버튼 ---
        Button(
            onClick = {
                if (!permissionState.allPermissionsGranted) {
                    permissionState.launchMultiplePermissionRequest()
                } else if (isScanning) {
                    bleViewModel.stopScan()
                } else {
                    bleViewModel.startScan()
                }
            },
            modifier = Modifier
                .fillMaxWidth()
                .height(52.dp),
            shape = RoundedCornerShape(26.dp),
            colors = ButtonDefaults.buttonColors(containerColor = MedicalBlueContainer),
            enabled = connectionState != BleConnectionState.CONNECTING && connectionState != BleConnectionState.RECONNECTING
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Default.Radar, contentDescription = null)
                Spacer(Modifier.width(8.dp))
                Text(
                    if (isScanning) "Stop Scan" else "Scan for Devices",
                    fontWeight = FontWeight.Bold
                )
            }
        }
    }
}

// ============================================================================
// [디바이스 행 컴포넌트] - 검색된 각 BLE 디바이스 표시
// ============================================================================
@Composable
private fun DeviceRow(
    device: ScannedDevice,
    isConnecting: Boolean,
    isConnected: Boolean,
    onConnectClick: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Surface(
                shape = CircleShape,
                color = MedicalBlueContainer.copy(alpha = 0.15f),
                modifier = Modifier.size(40.dp)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(Icons.Default.Watch, contentDescription = null, tint = MedicalBlueContainer)
                }
            }
            Spacer(Modifier.width(12.dp))
            Column {
                Text(
                    device.name,
                    fontWeight = FontWeight.Bold,
                    fontSize = 15.sp,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    "RSSI: ${device.rssi}dBm | ${device.address}",
                    fontSize = 11.sp,
                    color = Color.Gray
                )
            }
        }
        Button(
            onClick = onConnectClick,
            enabled = !isConnecting && !isConnected,
            colors = ButtonDefaults.buttonColors(
                containerColor = if (isConnected) Color(0xFF4CAF50)
                else MaterialTheme.colorScheme.surfaceVariant
            ),
            shape = RoundedCornerShape(20.dp)
        ) {
            Text(
                when {
                    isConnected -> "Connected"
                    isConnecting -> "..."
                    else -> "Connect"
                },
                color = if (isConnected) Color.White else MaterialTheme.colorScheme.onSurface,
                fontSize = 13.sp
            )
        }
    }
}
