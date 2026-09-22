package com.example.myapplication.ui.screens

import android.content.res.Configuration
import androidx.compose.animation.core.*
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Air
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.myapplication.R
import com.example.myapplication.ui.theme.ActiveGreen
import com.example.myapplication.ui.theme.MedicalBlueContainer
import com.example.myapplication.ui.theme.RespiSyncTheme

@Composable
fun MainScreen(onStartClick: () -> Unit) {
    val infiniteTransition = rememberInfiniteTransition(label = "HologramAnim")

    // 1. 호흡 수축 팽창 애니메이션 (Scale)
    val scale by infiniteTransition.animateFloat(
        initialValue = 0.94f,
        targetValue = 1.05f,
        animationSpec = infiniteRepeatable(
            animation = tween(2200, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "BreathScale"
    )

    // 2. 위아래로 둥둥 떠다니는 효과 (Floating Y)
    val floatY by infiniteTransition.animateFloat(
        initialValue = -6f,
        targetValue = 6f,
        animationSpec = infiniteRepeatable(
            animation = tween(2600, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "FloatingY"
    )

    // 3. 입체감을 주는 좌우 살짝 기울임 (Tilt Y)
    val tiltY by infiniteTransition.animateFloat(
        initialValue = -10f,
        targetValue = 10f,
        animationSpec = infiniteRepeatable(
            animation = tween(3200, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "TiltY"
    )

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.verticalGradient(
                    colors = listOf(
                        MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.2f),
                        MaterialTheme.colorScheme.background
                    )
                )
            )
            .padding(24.dp),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            modifier = Modifier.fillMaxSize(),
            verticalArrangement = Arrangement.SpaceBetween
        ) {
            Spacer(Modifier.height(12.dp))

            // App Header
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Surface(
                    shape = CircleShape,
                    color = MaterialTheme.colorScheme.surface,
                    shadowElevation = 4.dp,
                    modifier = Modifier.size(80.dp)
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Icon(
                            Icons.Default.Air,
                            contentDescription = null,
                            tint = MedicalBlueContainer,
                            modifier = Modifier.size(44.dp)
                        )
                    }
                }

                Spacer(Modifier.height(16.dp))

                Text(
                    text = "RespiSync",
                    style = MaterialTheme.typography.headlineLarge.copy(
                        color = MedicalBlueContainer,
                        fontWeight = FontWeight.Black,
                        fontSize = 34.sp
                    )
                )

                Spacer(Modifier.height(4.dp))

                Text(
                    text = "스마트 기도 청결 조끼 제어기",
                    style = MaterialTheme.typography.titleMedium.copy(
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                )
            }

            // 💡 4. 중앙 메인 조작부 (원 크기 복원 + 이미지 상단 이동 + 캡슐 제거)
            Box(
                modifier = Modifier
                    .size(270.dp)
                    .clip(CircleShape)
                    .clickable { onStartClick() },
                contentAlignment = Alignment.Center
            ) {
                // 외곽 호흡 애니메이션 펄스 링
                Box(
                    modifier = Modifier
                        .size(250.dp)
                        .scale(scale)
                        .clip(CircleShape)
                        .background(MedicalBlueContainer.copy(alpha = 0.15f))
                )

                // 메인 중앙 원형 버튼 (이전 규격 200dp 복원)
                Surface(
                    shape = CircleShape,
                    color = MaterialTheme.colorScheme.surface,
                    shadowElevation = 12.dp,
                    modifier = Modifier.size(200.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .background(
                                Brush.linearGradient(
                                    colors = listOf(Color(0xFF38BDF8), MedicalBlueContainer)
                                )
                            ),
                        contentAlignment = Alignment.Center
                    ) {
                        // 💡 중앙에서 문구 한 줄 크기(18dp)만큼 위로 올린 3D 폐 이미지
                        Box(
                            modifier = Modifier
                                .offset(y = (-18).dp)
                                .graphicsLayer {
                                    translationY = floatY * density
                                    rotationY = tiltY
                                    cameraDistance = 16f * density
                                },
                            contentAlignment = Alignment.Center
                        ) {
                            Image(
                                painter = painterResource(id = R.drawable.lung_3d),
                                contentDescription = "3D Lung Graphic",
                                modifier = Modifier.size(300.dp)
                            )
                        }

                        // 💡 캡슐(Surface) 없이 원 하단에 배치된 깔끔한 텍스트 문구
                        Text(
                            text = "터치하여 시작",
                            color = Color.White,
                            fontSize = 13.sp,
                            fontWeight = FontWeight.Bold,
                            letterSpacing = 0.5.sp,
                            modifier = Modifier
                                .align(Alignment.BottomCenter)
                                .padding(bottom = 16.dp)
                        )
                    }
                }
            }

            // Bottom Device Banner
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Surface(
                    color = MaterialTheme.colorScheme.surface,
                    shape = RoundedCornerShape(24.dp),
                    shadowElevation = 3.dp
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 20.dp, vertical = 10.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Box(
                            modifier = Modifier
                                .size(10.dp)
                                .clip(CircleShape)
                                .background(ActiveGreen)
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(
                            text = "Connected Device: ESP32_Vest_BLE",
                            fontSize = 13.sp,
                            fontWeight = FontWeight.SemiBold,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                }

                Spacer(Modifier.height(16.dp))

                Text(
                    text = "DEVELOPED BY AAOO",
                    color = Color.Gray,
                    fontSize = 11.sp,
                    letterSpacing = 2.sp
                )

                Spacer(Modifier.height(12.dp))
            }
        }
    }
}

// --------------------------------------------------
// PREVIEWS
// --------------------------------------------------

@Preview(
    name = "Main Screen - Light Theme",
    showBackground = true,
    showSystemUi = true
)
@Composable
fun MainScreenPreview() {
    RespiSyncTheme(darkTheme = false) {
        MainScreen(onStartClick = {})
    }
}

@Preview(
    name = "Main Screen - Dark Theme",
    showBackground = true,
    showSystemUi = true,
    uiMode = Configuration.UI_MODE_NIGHT_YES
)
@Composable
fun MainScreenDarkPreview() {
    RespiSyncTheme(darkTheme = true) {
        MainScreen(onStartClick = {})
    }
}