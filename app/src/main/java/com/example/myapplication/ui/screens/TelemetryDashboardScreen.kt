package com.example.myapplication.ui.screens

import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Error
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.myapplication.ble.BleConnectionState
import com.example.myapplication.ble.BleViewModel
import com.example.myapplication.ble.DeviceState
import com.example.myapplication.ble.RespirationPhase
import com.example.myapplication.ui.theme.ActiveGreen
import com.example.myapplication.ui.theme.MedicalBlueContainer
import com.example.myapplication.ui.theme.SoftCyanContainer

@Composable
fun TelemetryDashboardScreen(bleViewModel: BleViewModel = viewModel()) {

    // --- BLE 텔레메트리 데이터 실시간 구독 ---
    val connectionState by bleViewModel.connectionState.collectAsState()
    val telemetry by bleViewModel.telemetryData.collectAsState()
    val isConnected = connectionState == BleConnectionState.CONNECTED

    // --- 호흡 파형 히스토리 버퍼 (최근 100샘플 유지) ---
    val pressureHistory = remember { mutableStateListOf<Float>() }
    val maxHistorySize = 100

    // 텔레메트리 갱신될 때마다 히스토리에 추가
    LaunchedEffect(telemetry.chestPressure) {
        if (isConnected) {
            pressureHistory.add(telemetry.chestPressure.toFloat())
            if (pressureHistory.size > maxHistorySize) {
                pressureHistory.removeAt(0)
            }
        }
    }

    // --- 애니메이션 (미연결 시 데모 파형용) ---
    val infiniteTransition = rememberInfiniteTransition(label = "Wave")
    val phase by infiniteTransition.animateFloat(
        initialValue = 0f,
        targetValue = 2f * Math.PI.toFloat(),
        animationSpec = infiniteRepeatable(
            animation = tween(2200, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "Phase"
    )

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .verticalScroll(rememberScrollState())
            .padding(16.dp)
    ) {
        // =====================================================================
        // [1] 호흡 파형 그래프 - ESP32에서 수신한 실시간 흉부 센서값
        // =====================================================================
        Card(
            modifier = Modifier
                .fillMaxWidth()
                .height(210.dp),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        "Respiration Waveform",
                        fontWeight = FontWeight.Bold,
                        fontSize = 16.sp,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    // 호흡 위상 배지 (ESP32 Core 0 인지 결과)
                    Surface(
                        color = when (telemetry.respirationPhase) {
                            RespirationPhase.EXHALE -> SoftCyanContainer.copy(alpha = 0.15f)
                            RespirationPhase.INHALE -> Color(0xFFFFF3E0)
                            else -> Color.Gray.copy(alpha = 0.1f)
                        },
                        shape = RoundedCornerShape(12.dp)
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 10.dp, vertical = 4.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Box(
                                modifier = Modifier
                                    .size(8.dp)
                                    .background(
                                        when (telemetry.respirationPhase) {
                                            RespirationPhase.EXHALE -> SoftCyanContainer
                                            RespirationPhase.INHALE -> Color(0xFFFF9800)
                                            else -> Color.Gray
                                        },
                                        shape = RoundedCornerShape(4.dp)
                                    )
                            )
                            Spacer(Modifier.width(6.dp))
                            Text(
                                when (telemetry.respirationPhase) {
                                    RespirationPhase.EXHALE -> "Expiration"
                                    RespirationPhase.INHALE -> "Inspiration"
                                    else -> "Detecting..."
                                },
                                fontSize = 12.sp,
                                color = when (telemetry.respirationPhase) {
                                    RespirationPhase.EXHALE -> MedicalBlueContainer
                                    RespirationPhase.INHALE -> Color(0xFFE65100)
                                    else -> Color.Gray
                                },
                                fontWeight = FontWeight.Bold
                            )
                        }
                    }
                }
                Spacer(Modifier.height(16.dp))

                // 파형 캔버스
                Canvas(modifier = Modifier.fillMaxSize()) {
                    val w = size.width
                    val h = size.height
                    val midY = h / 2

                    // 중앙 기준선
                    drawLine(Color.LightGray.copy(0.3f), Offset(0f, midY), Offset(w, midY), strokeWidth = 2f)

                    if (isConnected && pressureHistory.size > 2) {
                        // ★ 실제 데이터 파형: ESP32에서 수신한 필터링 후 흉부 호흡 센서값
                        val wavePath = Path()
                        val maxVal = pressureHistory.maxOrNull() ?: 1f
                        val minVal = pressureHistory.minOrNull() ?: -1f
                        val peak = maxOf(kotlin.math.abs(maxVal), kotlin.math.abs(minVal)).coerceAtLeast(10f)

                        pressureHistory.forEachIndexed { index, value ->
                            val x = (index.toFloat() / maxHistorySize) * w
                            // 필터링 후 신호는 0 기준 대칭 진동: 중앙 기준선(midY)을 중심으로 위/아래 렌더링
                            val normalizedY = midY - (value / peak * (h / 2f) * 0.85f)

                            if (index == 0) wavePath.moveTo(x, normalizedY)
                            else wavePath.lineTo(x, normalizedY)
                        }
                        drawPath(wavePath, color = MedicalBlueContainer, style = Stroke(width = 4f))
                    } else {
                        // 미연결 시 데모 사인파
                        val wavePath = Path()
                        wavePath.moveTo(0f, midY)
                        for (x in 0..w.toInt() step 4) {
                            val relX = x.toFloat() / w
                            val y = midY + kotlin.math.sin(relX * 3 * Math.PI + phase).toFloat() * 40f
                            wavePath.lineTo(relX * w, y)
                        }
                        drawPath(wavePath, color = Color.Gray.copy(alpha = 0.4f), style = Stroke(width = 3f))
                    }
                }
            }
        }

        Spacer(Modifier.height(16.dp))

        // =====================================================================
        // [2] 실시간 수치 대시보드 - 텔레메트리 패킷 데이터 표시
        // =====================================================================
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // 흉부 센서 호흡 신호 카드 (필터링 후)
            MetricCard(
                modifier = Modifier.weight(1f),
                title = "RESPIRATION SIGNAL",
                value = if (isConnected) "${telemetry.chestPressure}" else "--",
                unit = "Filtered",
                color = MedicalBlueContainer
            )
            // 모터 상태 카드
            MetricCard(
                modifier = Modifier.weight(1f),
                title = "MOTOR",
                value = if (isConnected) {
                    if (telemetry.motorActive) "ON" else "OFF"
                } else "--",
                unit = "",
                color = if (telemetry.motorActive) Color(0xFF4CAF50) else Color.Gray
            )
        }

        Spacer(Modifier.height(12.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // 타격 주기 카드
            MetricCard(
                modifier = Modifier.weight(1f),
                title = "STRIKE PERIOD",
                value = if (isConnected) "${telemetry.currentPeriodMs}" else "--",
                unit = "ms",
                color = MedicalBlueContainer
            )
            // 장치 상태 카드
            MetricCard(
                modifier = Modifier.weight(1f),
                title = "DEVICE STATE",
                value = if (isConnected) {
                    when (telemetry.deviceState) {
                        DeviceState.IDLE -> "Idle"
                        DeviceState.RUNNING -> "Running"
                        DeviceState.CALIBRATING -> "Calib."
                        DeviceState.ERROR -> "Error!"
                    }
                } else "--",
                unit = "",
                color = when (telemetry.deviceState) {
                    DeviceState.RUNNING -> ActiveGreen
                    DeviceState.ERROR -> Color(0xFFE53935)
                    else -> Color.Gray
                }
            )
        }

        Spacer(Modifier.height(16.dp))

        // =====================================================================
        // [3] 연결 상태 & 에러 정보 카드
        // =====================================================================
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Text("System Status", fontWeight = FontWeight.Bold, fontSize = 16.sp, color = MaterialTheme.colorScheme.onSurface)
                Spacer(Modifier.height(12.dp))

                // 연결 상태
                StatusRow(
                    label = "BLE Connection",
                    value = when (connectionState) {
                        BleConnectionState.CONNECTED -> "Connected"
                        BleConnectionState.RECONNECTING -> "Reconnecting..."
                        BleConnectionState.CONNECTING -> "Connecting..."
                        else -> "Disconnected"
                    },
                    isOk = isConnected
                )
                Spacer(Modifier.height(8.dp))

                // 전원 상태
                StatusRow(
                    label = "Power Supply",
                    value = if (isConnected) "${telemetry.powerStatus}% (Wired)" else "--",
                    isOk = isConnected && telemetry.powerStatus > 0
                )
                Spacer(Modifier.height(8.dp))

                // 호흡 인지 상태
                StatusRow(
                    label = "Respiration Sensing",
                    value = if (isConnected) {
                        when (telemetry.respirationPhase) {
                            RespirationPhase.INHALE -> "Inhale detected"
                            RespirationPhase.EXHALE -> "Exhale detected"
                            RespirationPhase.NONE -> "Waiting..."
                        }
                    } else "--",
                    isOk = isConnected && telemetry.respirationPhase != RespirationPhase.NONE
                )
                Spacer(Modifier.height(8.dp))

                // 에러 코드
                StatusRow(
                    label = "Error Code",
                    value = if (isConnected) {
                        if (telemetry.errorCode == 0) "Normal" else "0x${String.format("%02X", telemetry.errorCode)}"
                    } else "--",
                    isOk = !isConnected || telemetry.errorCode == 0
                )
            }
        }


    }
}

// ============================================================================
// [수치 카드 컴포넌트] - 개별 텔레메트리 값 표시
// ============================================================================
@Composable
private fun MetricCard(
    modifier: Modifier = Modifier,
    title: String,
    value: String,
    unit: String,
    color: Color
) {
    Card(
        modifier = modifier,
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        shape = RoundedCornerShape(16.dp)
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(title, fontSize = 11.sp, color = Color.Gray, fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(8.dp))
            Row(verticalAlignment = Alignment.Bottom) {
                Text(
                    value,
                    fontSize = 24.sp,
                    fontWeight = FontWeight.Black,
                    color = color
                )
                if (unit.isNotEmpty()) {
                    Spacer(Modifier.width(4.dp))
                    Text(unit, fontSize = 12.sp, color = Color.Gray, fontWeight = FontWeight.Bold)
                }
            }
        }
    }
}

// ============================================================================
// [상태 행 컴포넌트] - 시스템 상태 목록 표시
// ============================================================================
@Composable
private fun StatusRow(label: String, value: String, isOk: Boolean) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(
                if (isOk) Icons.Default.CheckCircle else Icons.Default.Error,
                contentDescription = null,
                tint = if (isOk) ActiveGreen else Color(0xFFE53935),
                modifier = Modifier.size(16.dp)
            )
            Spacer(Modifier.width(8.dp))
            Text(label, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurface)
        }
        Text(value, fontSize = 13.sp, fontWeight = FontWeight.Bold, color = Color.Gray)
    }
}
