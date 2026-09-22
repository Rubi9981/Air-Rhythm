package com.example.myapplication.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val LightColorScheme = lightColorScheme(
    primary = MedicalBluePrimary,
    primaryContainer = MedicalBlueContainer,
    onPrimary = Color.White,
    secondary = SoftCyanSecondary,
    secondaryContainer = SoftCyanContainer,
    tertiary = MintGreenTarget,
    surface = SurfaceLight,
    onSurface = OnSurfaceLight,
    error = EmergencyRed,
    background = SurfaceLight
)

private val DarkColorScheme = darkColorScheme(
    primary = Color(0xFFB5C4FF),
    primaryContainer = MedicalBlueContainer,
    surface = Color(0xFF111827),
    onSurface = Color.White,
    background = Color(0xFF0B0F17)
)

@Composable
fun RespiSyncTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colors = if (darkTheme) DarkColorScheme else LightColorScheme
    MaterialTheme(
        colorScheme = colors,
        content = content
    )
}