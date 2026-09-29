package com.example.myapplication.ui.components

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.myapplication.model.RespirationPhase
import com.example.myapplication.ui.theme.ActiveGreen
import com.example.myapplication.ui.theme.MedicalBluePrimary
import com.example.myapplication.ui.theme.SoftCyanSecondary
import com.example.myapplication.viewmodel.PhaseSegment

@Composable
fun RespiWaveformCard(
    respirationRate: Int,
    ieRatio: String,
    currentPhase: RespirationPhase,
    waveformBuffer: List<Float> = emptyList(),
    phaseSegments: List<PhaseSegment> = emptyList(),
    isConnected: Boolean = false,
    modifier: Modifier = Modifier
) {
    val isDark = MaterialTheme.colorScheme.surface.luminance() < 0.5f
    val infiniteTransition = rememberInfiniteTransition(label = "pulseTransition")

    // 커서 ping 애니메이션 (BLE 연결 상태에서만 의미 있음)
    val pingScale by infiniteTransition.animateFloat(
        initialValue = 1f,
        targetValue = 2.2f,
        animationSpec = infiniteRepeatable(
            animation = tween(1200, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "pingScale"
    )
    val pingAlpha by infiniteTransition.animateFloat(
        initialValue = 0.8f,
        targetValue = 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(1200, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "pingAlpha"
    )

    val cyanColor  = SoftCyanSecondary
    val blueColor  = MedicalBluePrimary
    val grayColor  = Color(0xFF737686)

    Card(
        modifier = modifier
            .fillMaxWidth()
            .testTag("respi_waveform_card"),
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp)
        ) {
            // ── Header ───────────────────────────────────────────────────────
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "호흡 주기 파형 그래프",
                    style = MaterialTheme.typography.titleMedium.copy(
                        fontWeight = FontWeight.Bold,
                        fontSize = 18.sp
                    ),
                    color = MaterialTheme.colorScheme.onSurface
                )

                val badgeBg     = if (isDark) ActiveGreen.copy(alpha = 0.2f) else Color(0xFFECFDF5)
                val badgeBorder = if (isDark) ActiveGreen.copy(alpha = 0.45f) else Color(0xFFA7F3D0)
                val badgeText   = if (isDark) ActiveGreen else Color(0xFF047857)

                Row(
                    modifier = Modifier
                        .clip(RoundedCornerShape(999.dp))
                        .background(badgeBg)
                        .border(1.dp, badgeBorder, RoundedCornerShape(999.dp))
                        .padding(horizontal = 10.dp, vertical = 5.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Box(contentAlignment = Alignment.Center, modifier = Modifier.size(10.dp)) {
                        Box(
                            modifier = Modifier
                                .fillMaxSize()
                                .scale(pingScale)
                                .clip(CircleShape)
                                .background(ActiveGreen.copy(alpha = if (isConnected) pingAlpha else 0f))
                        )
                        Box(
                            modifier = Modifier
                                .size(6.dp)
                                .clip(CircleShape)
                                .background(ActiveGreen)
                        )
                    }
                    Text(
                        text = if (isConnected) "실시간 감지 중" else "연결 대기 중",
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Bold,
                        color = badgeText
                    )
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            // ── Graph Canvas ─────────────────────────────────────────────────
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(210.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f))
                    .border(
                        1.dp,
                        MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f),
                        RoundedCornerShape(14.dp)
                    )
            ) {
                Canvas(modifier = Modifier.fillMaxSize()) {
                    val w = size.width
                    val h = size.height

                    // ── 격자선 ────────────────────────────────────────────────
                    val gridDash = PathEffect.dashPathEffect(floatArrayOf(8f, 8f), 0f)
                    for (level in listOf(0.2f, 0.4f, 0.6f, 0.8f)) {
                        drawLine(
                            color = grayColor.copy(alpha = 0.25f),
                            start = Offset(0f, h * level),
                            end   = Offset(w, h * level),
                            strokeWidth = 1f,
                            pathEffect = gridDash
                        )
                    }

                    val bufSize = waveformBuffer.size

                    // ── BLE 미연결 또는 버퍼 없음 → flatline ──────────────────
                    if (!isConnected || bufSize < 2) {
                        val flatY = h * 0.5f
                        drawLine(
                            color = grayColor.copy(alpha = 0.35f),
                            start = Offset(0f, flatY),
                            end   = Offset(w, flatY),
                            strokeWidth = 2.dp.toPx(),
                            pathEffect = PathEffect.dashPathEffect(floatArrayOf(8f, 8f), 0f)
                        )
                        return@Canvas
                    }

                    // ── Y축 정규화 ─────────────────────────────────────────────
                    val minVal = waveformBuffer.min()
                    val maxVal = waveformBuffer.max()
                    val range  = (maxVal - minVal).let { if (it < 1f) 1f else it }
                    val margin = h * 0.12f  // 상하 여백

                    fun toPixelY(v: Float): Float {
                        val normalized = (v - minVal) / range   // 0.0 ~ 1.0
                        // 값이 클수록 위(작은 Y), 작을수록 아래(큰 Y)
                        return (h - margin) - normalized * (h - 2 * margin)
                    }

                    // X축: 버퍼의 각 샘플을 균등 간격으로 배치
                    fun toPixelX(idx: Int): Float = idx.toFloat() / (bufSize - 1).coerceAtLeast(1) * w

                    // ── 구간 배경 색 칠하기 ────────────────────────────────────
                    val vertDash = PathEffect.dashPathEffect(floatArrayOf(10f, 10f), 0f)

                    for (seg in phaseSegments) {
                        val segColor = when (seg.phase) {
                            RespirationPhase.INSPIRATION -> cyanColor
                            RespirationPhase.EXHALATION  -> blueColor
                        }
                        val startX = toPixelX(seg.startIndex.coerceIn(0, bufSize - 1))
                        val endX   = if (seg.endIndex == -1) w
                                     else toPixelX(seg.endIndex.coerceIn(0, bufSize - 1))

                        if (endX > startX) {
                            drawRect(
                                color    = segColor.copy(alpha = 0.08f),
                                topLeft  = Offset(startX, 0f),
                                size     = Size(endX - startX, h)
                            )
                            // 구간 시작 점선 경계
                            if (startX > 0f) {
                                drawLine(
                                    color       = segColor.copy(alpha = 0.6f),
                                    start       = Offset(startX, 0f),
                                    end         = Offset(startX, h),
                                    strokeWidth = 1.5f,
                                    pathEffect  = vertDash
                                )
                            }
                        }
                    }

                    // ── 파형 그리기 ────────────────────────────────────────────
                    // 구간별로 색을 분리하여 그림 (구간 없으면 단색 회색)
                    if (phaseSegments.isEmpty()) {
                        // 구간 정보 없으면 전체 회색 파형
                        val wavePath = Path().apply {
                            moveTo(toPixelX(0), toPixelY(waveformBuffer[0]))
                            for (i in 1 until bufSize) {
                                lineTo(toPixelX(i), toPixelY(waveformBuffer[i]))
                            }
                        }
                        drawPath(
                            path  = wavePath,
                            color = grayColor.copy(alpha = 0.6f),
                            style = Stroke(width = 2.dp.toPx(), cap = StrokeCap.Round)
                        )
                    } else {
                        // 구간별로 색을 다르게 파형 그리기
                        // 구간이 커버하지 않는 맨 앞 영역은 회색(과거 데이터)
                        val firstSegStart = phaseSegments.firstOrNull()?.startIndex ?: 0
                        if (firstSegStart > 0) {
                            val pastPath = Path().apply {
                                moveTo(toPixelX(0), toPixelY(waveformBuffer[0]))
                                for (i in 1..firstSegStart.coerceAtMost(bufSize - 1)) {
                                    lineTo(toPixelX(i), toPixelY(waveformBuffer[i]))
                                }
                            }
                            drawPath(
                                path  = pastPath,
                                color = grayColor.copy(alpha = 0.4f),
                                style = Stroke(
                                    width      = 2.dp.toPx(),
                                    cap        = StrokeCap.Round,
                                    pathEffect = PathEffect.dashPathEffect(floatArrayOf(6f, 6f), 0f)
                                )
                            )
                        }

                        // 각 구간별 파형
                        for (seg in phaseSegments) {
                            val segColor   = when (seg.phase) {
                                RespirationPhase.INSPIRATION -> cyanColor
                                RespirationPhase.EXHALATION  -> blueColor
                            }
                            val strokeW    = when (seg.phase) {
                                RespirationPhase.INSPIRATION -> 2.5.dp.toPx()
                                RespirationPhase.EXHALATION  -> 3.dp.toPx()
                            }
                            val isCurrent  = seg.endIndex == -1
                            val startIdx   = seg.startIndex.coerceIn(0, bufSize - 1)
                            val endIdx     = if (seg.endIndex == -1) bufSize - 1
                                             else seg.endIndex.coerceIn(0, bufSize - 1)

                            if (startIdx >= endIdx) continue

                            val segPath = Path().apply {
                                moveTo(toPixelX(startIdx), toPixelY(waveformBuffer[startIdx]))
                                for (i in (startIdx + 1)..endIdx) {
                                    lineTo(toPixelX(i), toPixelY(waveformBuffer[i]))
                                }
                            }

                            // 현재 진행 중인 구간은 fill 추가
                            if (isCurrent) {
                                val fillPath = Path().apply {
                                    addPath(segPath)
                                    lineTo(toPixelX(endIdx), h)
                                    lineTo(toPixelX(startIdx), h)
                                    close()
                                }
                                drawPath(
                                    path  = fillPath,
                                    brush = Brush.verticalGradient(
                                        colors = listOf(segColor.copy(alpha = 0.22f), Color.Transparent),
                                        startY = 0f,
                                        endY   = h
                                    )
                                )
                            }

                            drawPath(
                                path  = segPath,
                                color = if (isCurrent) segColor else segColor.copy(alpha = 0.7f),
                                style = Stroke(width = strokeW, cap = StrokeCap.Round)
                            )
                        }
                    }

                    // ── 현재 커서 (우측 끝 최신 값) ────────────────────────────
                    val cursorX = w
                    val cursorY = toPixelY(waveformBuffer.last())
                    val cursorColor = when (currentPhase) {
                        RespirationPhase.INSPIRATION -> cyanColor
                        RespirationPhase.EXHALATION  -> blueColor
                    }
                    drawCircle(
                        color  = cursorColor.copy(alpha = pingAlpha),
                        radius = 10.dp.toPx() * pingScale,
                        center = Offset(cursorX, cursorY),
                        style  = Stroke(width = 1.5.dp.toPx())
                    )
                    drawCircle(
                        color  = cursorColor,
                        radius = 5.dp.toPx(),
                        center = Offset(cursorX, cursorY)
                    )
                }

                // ── 구간 배지 오버레이 (동적 위치) ──────────────────────────────
                // 현재 진행 중인 구간과 직전 완료 구간의 배지를 표시
                if (isConnected && waveformBuffer.isNotEmpty()) {
                    val bufSize = waveformBuffer.size

                    // 현재 진행 중인 구간 (endIndex == -1)
                    val currentSeg  = phaseSegments.lastOrNull { it.endIndex == -1 }
                    // 직전 완료된 구간
                    val previousSeg = phaseSegments.lastOrNull { it.endIndex != -1 }

                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(top = 10.dp)
                    ) {
                        // 직전 구간 배지
                        previousSeg?.let { seg ->
                            val midFraction = if (bufSize > 1) {
                                val midIdx = (seg.startIndex + seg.endIndex) / 2f
                                midIdx / (bufSize - 1).coerceAtLeast(1)
                            } else 0.5f

                            val segColor = when (seg.phase) {
                                RespirationPhase.INSPIRATION -> cyanColor
                                RespirationPhase.EXHALATION  -> blueColor
                            }
                            val badgeLabel = when (seg.phase) {
                                RespirationPhase.INSPIRATION -> "직전 흡기"
                                RespirationPhase.EXHALATION  -> "직전 호기"
                            }

                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.Start
                            ) {
                                Spacer(modifier = Modifier.fillMaxWidth(midFraction.coerceIn(0.01f, 0.85f)))
                                Row(
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(999.dp))
                                        .background(Color.White.copy(alpha = 0.92f))
                                        .border(1.dp, segColor.copy(alpha = 0.5f), RoundedCornerShape(999.dp))
                                        .padding(horizontal = 8.dp, vertical = 3.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                                ) {
                                    Box(
                                        modifier = Modifier
                                            .size(6.dp)
                                            .clip(CircleShape)
                                            .background(segColor)
                                    )
                                    Text(
                                        text       = badgeLabel,
                                        fontSize   = 11.sp,
                                        fontWeight = FontWeight.SemiBold,
                                        color      = segColor
                                    )
                                }
                            }
                        }

                        // 현재 진행 중 구간 배지
                        currentSeg?.let { seg ->
                            val startFraction = if (bufSize > 1) {
                                seg.startIndex.toFloat() / (bufSize - 1).coerceAtLeast(1)
                            } else 0.6f
                            // 배지는 구간 시작 ~ 끝(우측) 의 중간에 표시
                            val midFraction = ((startFraction + 1f) / 2f).coerceIn(0.4f, 0.88f)

                            val segColor = when (seg.phase) {
                                RespirationPhase.INSPIRATION -> cyanColor
                                RespirationPhase.EXHALATION  -> blueColor
                            }
                            val badgeLabel = when (seg.phase) {
                                RespirationPhase.INSPIRATION -> "흡기 중"
                                RespirationPhase.EXHALATION  -> "호기 중"
                            }

                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(top = 28.dp),  // 직전 배지와 겹치지 않도록
                                horizontalArrangement = Arrangement.Start
                            ) {
                                Spacer(modifier = Modifier.fillMaxWidth(midFraction))
                                Row(
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(999.dp))
                                        .background(segColor.copy(alpha = 0.15f))
                                        .border(1.dp, segColor.copy(alpha = 0.4f), RoundedCornerShape(999.dp))
                                        .padding(horizontal = 8.dp, vertical = 3.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                                ) {
                                    Box(
                                        modifier = Modifier
                                            .size(6.dp)
                                            .clip(CircleShape)
                                            .background(segColor)
                                    )
                                    Text(
                                        text       = badgeLabel,
                                        fontSize   = 11.sp,
                                        fontWeight = FontWeight.Bold,
                                        color      = segColor
                                    )
                                }
                            }
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            // ── Bottom Metrics ────────────────────────────────────────────────
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .clip(RoundedCornerShape(12.dp))
                        .background(MaterialTheme.colorScheme.surfaceVariant)
                        .border(
                            1.dp,
                            MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f),
                            RoundedCornerShape(12.dp)
                        )
                        .padding(vertical = 12.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(
                            text       = "호흡수 (RR)",
                            fontSize   = 12.sp,
                            fontWeight = FontWeight.Medium,
                            color      = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(modifier = Modifier.height(2.dp))
                        Row(
                            verticalAlignment    = Alignment.Bottom,
                            horizontalArrangement = Arrangement.Center
                        ) {
                            Text(
                                text       = "$respirationRate",
                                fontSize   = 26.sp,
                                fontWeight = FontWeight.Bold,
                                color      = MaterialTheme.colorScheme.primary,
                                lineHeight = 30.sp
                            )
                            Spacer(modifier = Modifier.width(4.dp))
                            Text(
                                text       = "BPM",
                                fontSize   = 12.sp,
                                fontWeight = FontWeight.Medium,
                                color      = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier   = Modifier.padding(bottom = 2.dp)
                            )
                        }
                    }
                }

                Box(
                    modifier = Modifier
                        .weight(1f)
                        .clip(RoundedCornerShape(12.dp))
                        .background(MaterialTheme.colorScheme.surfaceVariant)
                        .border(
                            1.dp,
                            MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f),
                            RoundedCornerShape(12.dp)
                        )
                        .padding(vertical = 12.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(
                            text       = "시간당 호흡비(호기: 흡기)",
                            fontSize   = 11.sp,
                            fontWeight = FontWeight.Medium,
                            color      = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(modifier = Modifier.height(2.dp))
                        Text(
                            text       = ieRatio,
                            fontSize   = 26.sp,
                            fontWeight = FontWeight.Bold,
                            color      = MaterialTheme.colorScheme.primary,
                            lineHeight = 30.sp
                        )
                    }
                }
            }
        }
    }
}