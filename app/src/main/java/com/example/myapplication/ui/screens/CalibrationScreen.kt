package com.example.myapplication.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.myapplication.ui.theme.EmergencyRed
import com.example.myapplication.ui.theme.MedicalBlueContainer

@Composable
fun CalibrationScreen() {
    var autoReconnect by remember { mutableStateOf(true) }
    var hapticFeedback by remember { mutableStateOf(true) }
    var safetyFallbackMode by remember { mutableStateOf(true) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .verticalScroll(rememberScrollState())
            .padding(16.dp)
    ) {
        // 💡 본문 상단 중복 검은색 타이틀 제거 완료!

        // 1. Gyro Zero-Point Card
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.Explore, contentDescription = null, tint = MedicalBlueContainer)
                    Spacer(Modifier.width(8.dp))
                    Text("Gyroscope Zero-Point", fontWeight = FontWeight.Bold, fontSize = 17.sp, color = MaterialTheme.colorScheme.onSurface)
                }
                Spacer(Modifier.height(8.dp))
                Text(
                    "Establish a neutral baseline for respiratory tracking. Ensure patient is stationary.",
                    color = Color.Gray,
                    fontSize = 12.sp
                )
                Spacer(Modifier.height(12.dp))
                Surface(color = MaterialTheme.colorScheme.surfaceVariant, shape = RoundedCornerShape(8.dp)) {
                    Row(modifier = Modifier.padding(12.dp).fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.Info, contentDescription = null, tint = MedicalBlueContainer, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(8.dp))
                        Text("Instruction: Sit Upright and Still", fontSize = 12.sp, fontWeight = FontWeight.Medium, color = MaterialTheme.colorScheme.onSurface)
                    }
                }
                Spacer(Modifier.height(14.dp))
                Button(
                    onClick = {},
                    modifier = Modifier.fillMaxWidth().height(46.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = MedicalBlueContainer)
                ) {
                    Icon(Icons.Default.Adjust, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Set Gyro 0-Point", fontWeight = FontWeight.Bold)
                }
            }
        }

        Spacer(Modifier.height(16.dp))

        // 2. Hardware Modes & Safety Fallback Mode Toggles
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Text("Safety & Hardware Modes", fontWeight = FontWeight.Bold, fontSize = 17.sp, color = MaterialTheme.colorScheme.onSurface)
                Spacer(Modifier.height(12.dp))

                ToggleItem("Auto-Reconnect", "Automatically attempt BLE connection.", autoReconnect) { autoReconnect = it }
                HorizontalDivider(Modifier.padding(vertical = 8.dp), color = MaterialTheme.colorScheme.outlineVariant)

                ToggleItem("Haptic Feedback", "Vibrate device on alert thresholds.", hapticFeedback) { hapticFeedback = it }
                HorizontalDivider(Modifier.padding(vertical = 8.dp), color = MaterialTheme.colorScheme.outlineVariant)

                // 💡 Safety Fallback Mode (응급 강화를 위해 빨간색으로 스타일링)
                SafetyFallbackToggleItem(
                    title = "Safety Fallback Mode",
                    desc = "Emergency vibration stop if BLE connection or sensor signal is lost.",
                    checked = safetyFallbackMode,
                    onCheckedChange = { safetyFallbackMode = it }
                )
            }
        }

        Spacer(Modifier.height(16.dp))

        // 3. Device Information Card
        // TODO: [Prototype Mode] 프로토타입 시연을 위해 항상 표출되도록 설정함. 실기기 연결 상태 처리 시 아래 카드를 주석 처리 가능.
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("Device Information", fontWeight = FontWeight.Bold, fontSize = 17.sp, color = MaterialTheme.colorScheme.onSurface)
                    Surface(color = MedicalBlueContainer.copy(alpha = 0.15f), shape = RoundedCornerShape(12.dp)) {
                        Text("PROTOTYPE", modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp), fontSize = 10.sp, fontWeight = FontWeight.Bold, color = MedicalBlueContainer)
                    }
                }
                Spacer(Modifier.height(12.dp))

                InfoRow("Hardware Name", "ESP32_Vest_BLE")
                InfoRow("MAC Address", "24:DC:C3:9A:B1:04")
                InfoRow("Firmware Version", "v2.1.0-RC3")
                InfoRow("Battery Status", "88% (Charging)")
                InfoRow("BLE Signal (RSSI)", "-62 dBm (Strong)")
            }
        }


    }
}

@Composable
fun ToggleItem(title: String, desc: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, fontWeight = FontWeight.Bold, fontSize = 14.sp, color = MaterialTheme.colorScheme.onSurface)
            Text(desc, color = Color.Gray, fontSize = 11.sp)
        }
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            colors = SwitchDefaults.colors(checkedThumbColor = Color.White, checkedTrackColor = MedicalBlueContainer)
        )
    }
}

// 💡 응급 전용 빨간색 Safety Fallback 스위치 컴포넌트
@Composable
fun SafetyFallbackToggleItem(title: String, desc: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    Surface(
        color = EmergencyRed.copy(alpha = 0.08f),
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier.padding(12.dp).fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.Warning, contentDescription = null, tint = EmergencyRed, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(title, fontWeight = FontWeight.Bold, fontSize = 14.sp, color = EmergencyRed)
                }
                Spacer(Modifier.height(2.dp))
                Text(desc, color = EmergencyRed.copy(alpha = 0.8f), fontSize = 11.sp)
            }
            Switch(
                checked = checked,
                onCheckedChange = onCheckedChange,
                colors = SwitchDefaults.colors(
                    checkedThumbColor = Color.White,
                    checkedTrackColor = EmergencyRed,
                    uncheckedThumbColor = Color.White,
                    uncheckedTrackColor = Color.LightGray
                )
            )
        }
    }
}

@Composable
fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(label, fontSize = 12.sp, color = Color.Gray, fontWeight = FontWeight.Medium)
        Text(value, fontSize = 12.sp, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.onSurface)
    }
}