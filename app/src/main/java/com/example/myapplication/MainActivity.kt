package com.example.myapplication

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.Scaffold
import androidx.compose.material3.rememberDrawerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.ContextCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.myapplication.model.NavTab
import com.example.myapplication.ui.components.RespiBottomBar
import com.example.myapplication.ui.components.RespiDrawerContent
import com.example.myapplication.ui.components.RespiTopBar
import com.example.myapplication.ui.components.SettingsDialog
import com.example.myapplication.ui.screens.DashboardScreen
import com.example.myapplication.ui.screens.MainScreen
import com.example.myapplication.ui.screens.PairingScreen
import com.example.myapplication.ui.theme.RespiSyncTheme
import com.example.myapplication.viewmodel.RespiSyncViewModel
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            val viewModel: RespiSyncViewModel = viewModel()
            val uiState by viewModel.uiState.collectAsState()
            RespiSyncTheme(darkTheme = uiState.isDarkTheme) {
                RespiSyncApp(viewModel = viewModel)
            }
        }
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    fun RespiSyncApp(viewModel: RespiSyncViewModel) {
        val uiState by viewModel.uiState.collectAsState()
        val drawerState = rememberDrawerState(initialValue = DrawerValue.Closed)
        val coroutineScope = rememberCoroutineScope()
        val context = LocalContext.current

        val requiredPermissions = remember {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                arrayOf(
                    Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT
                )
            } else {
                arrayOf(
                    Manifest.permission.ACCESS_FINE_LOCATION
                )
            }
        }

        val permissionLauncher = rememberLauncherForActivityResult(
            contract = ActivityResultContracts.RequestMultiplePermissions()
        ) { permissionsMap ->
            val allGranted = permissionsMap.values.all { it }
            if (allGranted) {
                viewModel.startScan()
            }
        }

        val startScanWithPermission: () -> Unit = {
            val allGranted = requiredPermissions.all { perm ->
                ContextCompat.checkSelfPermission(context, perm) == PackageManager.PERMISSION_GRANTED
            }
            if (allGranted) {
                viewModel.startScan()
            } else {
                permissionLauncher.launch(requiredPermissions)
            }
        }

        LaunchedEffect(uiState.isStarted) {
            if (uiState.isStarted && !uiState.isConnected) {
                startScanWithPermission()
            }
        }

        if (!uiState.isStarted) {
            MainScreen(onStartClick = { viewModel.startApp() })
            return
        }

        BackHandler(enabled = drawerState.isOpen || uiState.currentTab != NavTab.PAIRING) {
            if (drawerState.isOpen) {
                coroutineScope.launch { drawerState.close() }
            } else if (uiState.currentTab != NavTab.PAIRING) {
                viewModel.setTab(NavTab.PAIRING)
            }
        }

        ModalNavigationDrawer(
            drawerState = drawerState,
            drawerContent = {
                RespiDrawerContent(
                    state = uiState,
                    onTabSelected = { tab: NavTab ->
                        viewModel.setTab(tab)
                        coroutineScope.launch { drawerState.close() }
                    },
                    onCloseDrawer = { coroutineScope.launch { drawerState.close() } },
                    onToggleDarkTheme = { isDark -> viewModel.toggleDarkTheme(isDark) }
                )
            }
        ) {
            Scaffold(
                topBar = {
                    RespiTopBar(
                        deviceName = uiState.connectedDeviceName,
                        isConnected = uiState.isConnected,
                        onMenuClick = { coroutineScope.launch { drawerState.open() } },
                        onSettingsClick = { viewModel.setSettingsDialogVisible(true) }
                    )
                },
                bottomBar = {
                    RespiBottomBar(
                        currentTab = uiState.currentTab,
                        onTabSelected = { tab: NavTab -> viewModel.setTab(tab) },
                        onOpenSettings = { viewModel.setSettingsDialogVisible(true) }
                    )
                }
            ) { paddingValues ->
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(paddingValues)
                ) {
                    when (uiState.currentTab) {
                        NavTab.DASHBOARD -> {
                            DashboardScreen(
                                state = uiState,
                                onModeToggle = { viewModel.toggleDetectionMode() },
                                onPhaseChange = { },
                                onIntensityChange = { level -> viewModel.setIntensity(level) }
                            )
                        }
                        NavTab.PAIRING -> {
                            PairingScreen(
                                state = uiState,
                                onStartScan = startScanWithPermission,
                                onConnectDevice = { id -> viewModel.connectDevice(id) },
                                onDisconnectDevice = { viewModel.disconnectDevice() },
                                onToggleAutoReconnect = { enabled -> viewModel.toggleAutoReconnect(enabled) },
                                onRenameDevice = { }
                            )
                        }
                    }
                }
            }
        }

        if (uiState.showSettingsDialog) {
            SettingsDialog(
                isDarkMode = uiState.isDarkTheme,
                connectedDeviceName = uiState.connectedDeviceName,
                deviceStatus = uiState.deviceStatus,
                onDarkModeChange = { viewModel.toggleDarkTheme(it) },
                onDismiss = { viewModel.setSettingsDialogVisible(false) }
            )
        }
    }
}