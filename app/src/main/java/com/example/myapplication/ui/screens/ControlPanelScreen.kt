package com.example.myapplication.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Vibration
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.myapplication.ble.BleConnectionState
import com.example.myapplication.ble.BleViewModel
import com.example.myapplication.ble.DeviceState
import com.example.myapplication.ui.theme.MedicalBlueContainer
import com.example.myapplication.ui.theme.VestGrayBackground

@Composable
fun ControlPanelScreen(bleViewModel: BleViewModel = viewModel()) {

    // --- BLE 상태 구독 ---
    val connectionState by bleViewModel.connectionState.collectAsState()
    val telemetry by bleViewModel.telemetryData.collectAsState()
    val isConnected = connectionState == BleConnectionState.CONNECTED
    val isRunning = telemetry.deviceState == DeviceState.RUNNING

    // --- UI 상태 ---
    var intensityLevel by remember { mutableIntStateOf(3) } // 1 ~ 5 단계 (추후 강도 제어 확장용)
    // 타격 주기를 Hz 단위로 표시 (내부적으로는 ms 단위로 ESP32에 전송)
    var hzFrequency by remember { mutableStateOf(2.0f) }  // 2Hz = 500ms 주기
    val zoneActiveStates = remember { mutableStateListOf(true, true, false, false, true, true) }

    // Hz를 ms로 변환하는 함수 (ESP32는 ms 단위로 받음)
    val periodMs: Int = (1000f / hzFrequency).toInt().coerceIn(200, 2000)

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .verticalScroll(rememberScrollState())
            .padding(16.dp)
    ) {
        // --- 연결 상태 표시 + 동작 상태 배지 ---
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(
                selected = isConnected || connectionState == BleConnectionState.RECONNECTING,
                onClick = {},
                label = {
                    Text(
                        when (connectionState) {
                            BleConnectionState.CONNECTED -> "BLE: Connected"
                            BleConnectionState.RECONNECTING -> "BLE: Reconnecting..."
                            BleConnectionState.CONNECTING -> "BLE: Connecting..."
                            else -> "BLE: Disconnected"
                        },
                        fontSize = 12.sp
                    )
                },
                leadingIcon = {
                    Icon(
                        Icons.Default.CheckCircle,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp),
                        tint = when (connectionState) {
                            BleConnectionState.CONNECTED -> Color(0xFF4CAF50)
                            BleConnectionState.RECONNECTING -> Color(0xFFFFA000)
                            else -> Color.Gray
                        }
                    )
                }
            )
            FilterChip(
                selected = isRunning,
                onClick = {},
                label = {
                    Text(
                        if (isRunning) "Motor: Running" else "Motor: Idle",
                        fontSize = 12.sp
                    )
                },
                leadingIcon = {
                    Icon(
                        if (isRunning) Icons.Default.Vibration else Icons.Default.Stop,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp)
                    )
                }
            )
        }

        Spacer(Modifier.height(12.dp))

        // =====================================================================
        // [START / STOP 제어 버튼] - 실제 BLE 명령 전송
        // =====================================================================
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Text(
                    "Strike Control",
                    fontWeight = FontWeight.Bold,
                    fontSize = 18.sp,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Spacer(Modifier.height(8.dp))
                Text(
                    "현재 주기: ${periodMs}ms (${String.format("%.1f", hzFrequency)}Hz)",
                    fontSize = 13.sp,
                    color = Color.Gray
                )
                // ESP32에서 실제 적용 중인 주기 표시
                if (isRunning) {
                    Text(
                        "ESP32 적용 중: ${telemetry.currentPeriodMs}ms",
                        fontSize = 12.sp,
                        color = MedicalBlueContainer
                    )
                }
                Spacer(Modifier.height(16.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    // START 버튼
                    Button(
                        onClick = {
                            bleViewModel.sendStart(periodMs)
                        },
                        enabled = isConnected && !isRunning,
                        modifier = Modifier.weight(1f).height(52.dp),
                        shape = RoundedCornerShape(16.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = Color(0xFF4CAF50),
                            disabledContainerColor = Color(0xFF4CAF50).copy(alpha = 0.3f)
                        )
                    ) {
                        Icon(Icons.Default.PlayArrow, contentDescription = null)
                        Spacer(Modifier.width(4.dp))
                        Text("START", fontWeight = FontWeight.Bold)
                    }

                    // STOP 버튼
                    Button(
                        onClick = {
                            bleViewModel.sendStop()
                        },
                        enabled = isConnected && isRunning,
                        modifier = Modifier.weight(1f).height(52.dp),
                        shape = RoundedCornerShape(16.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = Color(0xFFFF7043),
                            disabledContainerColor = Color(0xFFFF7043).copy(alpha = 0.3f)
                        )
                    ) {
                        Icon(Icons.Default.Stop, contentDescription = null)
                        Spacer(Modifier.width(4.dp))
                        Text("STOP", fontWeight = FontWeight.Bold)
                    }
                }
            }
        }

        Spacer(Modifier.height(16.dp))

        // =====================================================================
        // Zone Control Card
        // =====================================================================
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    "Zone Control",
                    fontWeight = FontWeight.Bold,
                    fontSize = 18.sp,
                    modifier = Modifier.align(Alignment.Start),
                    color = MaterialTheme.colorScheme.onSurface
                )

                Spacer(Modifier.height(16.dp))

                // 조끼 실루엣 컨테이너
                Box(
                    modifier = Modifier
                        .size(190.dp, 220.dp)
                        .background(VestGrayBackground, shape = RoundedCornerShape(32.dp)),
                    contentAlignment = Alignment.Center
                ) {
                    Column(
                        modifier = Modifier.fillMaxHeight(),
                        verticalArrangement = Arrangement.SpaceEvenly,
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        for (row in 0..2) {
                            Row(
                                modifier = Modifier.width(135.dp),
                                horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                for (col in 0..1) {
                                    val idx = row * 2 + col
                                    val isActive = zoneActiveStates[idx]
                                    Box(
                                        modifier = Modifier
                                            .size(48.dp)
                                            .clip(CircleShape)
                                            .background(if (isActive) Color.White else Color(0xFFC0C4C8))
                                            .clickable { zoneActiveStates[idx] = !zoneActiveStates[idx] },
                                        contentAlignment = Alignment.Center
                                    ) {
                                        if (isActive) {
                                            Icon(
                                                Icons.Default.Vibration,
                                                contentDescription = null,
                                                tint = MedicalBlueContainer,
                                                modifier = Modifier.size(26.dp)
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Spacer(Modifier.height(12.dp))
                Text("${zoneActiveStates.count { it }} of 6 Zones Active", color = Color.Gray, fontSize = 13.sp)
            }
        }

        Spacer(Modifier.height(16.dp))

        // =====================================================================
        // Intensity Dial (추후 강도 조절 명령 확장용)
        // =====================================================================
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("Intensity Level", fontWeight = FontWeight.Bold, fontSize = 18.sp, color = MaterialTheme.colorScheme.onSurface)
                    Text("Level $intensityLevel", fontSize = 22.sp, fontWeight = FontWeight.Black, color = MedicalBlueContainer)
                }

                Spacer(Modifier.height(16.dp))

                // 1~5단계 스텝 셀렉터 UI
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(48.dp)
                        .background(MaterialTheme.colorScheme.surfaceVariant, shape = CircleShape)
                        .padding(horizontal = 8.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    for (step in 1..5) {
                        val isSelected = step == intensityLevel
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .fillMaxHeight()
                                .padding(vertical = 4.dp)
                                .clip(CircleShape)
                                .background(if (isSelected) MedicalBlueContainer else Color.Transparent)
                                .clickable {
                                    intensityLevel = step
                                    // [TODO] 추후 강도 조절 명령 추가 시 여기서 전송:
                                    // bleViewModel.sendSetIntensity(step)
                                },
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = "$step",
                                color = if (isSelected) Color.White else Color.Gray,
                                fontWeight = if (isSelected) FontWeight.Black else FontWeight.Bold,
                                fontSize = 14.sp
                            )
                        }
                    }
                }

                Spacer(Modifier.height(8.dp))
                Text(
                    "※ 강도 조절은 추후 PWM 모터 지원 시 활성화됩니다",
                    fontSize = 11.sp,
                    color = Color.Gray
                )
            }
        }

        Spacer(Modifier.height(16.dp))

        // =====================================================================
        // Cycle Interval (타격 주기 조절) - 실시간 BLE 전송
        // =====================================================================
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Row(
                modifier = Modifier
                    .padding(16.dp)
                    .fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    Text("CYCLE INTERVAL", fontSize = 11.sp, color = Color.Gray, fontWeight = FontWeight.Bold)
                    Row(verticalAlignment = Alignment.Bottom) {
                        Text("$hzFrequency", fontSize = 28.sp, fontWeight = FontWeight.Black, color = MaterialTheme.colorScheme.onSurface)
                        Spacer(Modifier.width(4.dp))
                        Text("Hz", fontSize = 14.sp, fontWeight = FontWeight.Bold, color = Color.Gray)
                    }
                    Text("= ${periodMs}ms", fontSize = 12.sp, color = MedicalBlueContainer)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    IconButton(
                        onClick = {
                            if (hzFrequency > 0.5f) {
                                hzFrequency -= 0.5f
                                // 동작 중이면 실시간으로 주기 변경 전송
                                if (isRunning && isConnected) {
                                    val newPeriod = (1000f / hzFrequency).toInt().coerceIn(200, 2000)
                                    bleViewModel.sendSetPeriod(newPeriod)
                                }
                            }
                        },
                        modifier = Modifier.background(MedicalBlueContainer.copy(alpha = 0.15f), CircleShape)
                    ) {
                        Icon(Icons.Default.Remove, contentDescription = null, tint = MedicalBlueContainer)
                    }
                    IconButton(
                        onClick = {
                            if (hzFrequency < 5.0f) {  // 최대 5Hz (200ms)
                                hzFrequency += 0.5f
                                if (isRunning && isConnected) {
                                    val newPeriod = (1000f / hzFrequency).toInt().coerceIn(200, 2000)
                                    bleViewModel.sendSetPeriod(newPeriod)
                                }
                            }
                        },
                        modifier = Modifier.background(MedicalBlueContainer.copy(alpha = 0.15f), CircleShape)
                    ) {
                        Icon(Icons.Default.Add, contentDescription = null, tint = MedicalBlueContainer)
                    }
                }
            }
        }


    }
}
