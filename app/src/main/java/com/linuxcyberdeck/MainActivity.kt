package com.linuxcyberdeck

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.linuxcyberdeck.databinding.ActivityMainBinding
import com.linuxcyberdeck.native.LinuxNativeBridge
import com.linuxcyberdeck.session.LinuxSessionManager
import com.linuxcyberdeck.session.LinuxSessionState
import com.linuxcyberdeck.session.LinuxSessionStatus
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {

    private lateinit var binding: ActivityMainBinding
    private val sessionManager: LinuxSessionManager by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { view, insets ->
            val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            view.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
            insets
        }

        setupObservers()
        setupClickListeners()
    }

    private fun setupObservers() {
        sessionManager.sessionStatus.observe(this) { status ->
            updateUI(status)
        }

        sessionManager.isLoading.observe(this) { isLoading ->
            binding.btnMainAction.isEnabled = !isLoading
            binding.btnRestartLinux.isEnabled = !isLoading
            binding.btnStopLinux.isEnabled = !isLoading
            if (isLoading) {
                binding.btnMainAction.text = getString(R.string.status_starting)
            }
        }
    }

    private fun setupClickListeners() {
        binding.btnMainAction.setOnClickListener {
            val status = sessionManager.sessionStatus.value
            when (status?.state) {
                LinuxSessionState.NOT_INSTALLED -> showInstallDialog()
                LinuxSessionState.INSTALLED, LinuxSessionState.ERROR -> sessionManager.startLinux()
                LinuxSessionState.RUNNING -> openLinuxDesktop()
                else -> {}
            }
        }

        binding.btnOpenDesktop.setOnClickListener {
            openLinuxDesktop()
        }

        binding.btnOpenTerminal.setOnClickListener {
            openTerminal()
        }

        binding.btnOpenFirefox.setOnClickListener {
            openFirefox()
        }

        binding.btnOpenFiles.setOnClickListener {
            openFiles()
        }

        binding.btnRestartLinux.setOnClickListener {
            sessionManager.restartLinux()
        }

        binding.btnStopLinux.setOnClickListener {
            sessionManager.stopLinux()
        }

        binding.btnSettings.setOnClickListener {
            openSettings()
        }

        binding.btnErrorRestart.setOnClickListener {
            binding.cardError.visibility = View.GONE
            sessionManager.restartLinux()
        }

        binding.btnErrorViewLog.setOnClickListener {
            openDiagnostics()
        }
    }

    private fun updateUI(status: LinuxSessionStatus?) {
        status?.let { s ->
            // Status dot and text
            when (s.state) {
                LinuxSessionState.NOT_INSTALLED -> {
                    binding.viewStatusDot.background = getDrawable(R.drawable.status_dot_stopped)
                    binding.tvStatusText.text = getString(R.string.status_stopped)
                    binding.btnMainAction.text = getString(R.string.btn_start_linux)
                    binding.btnMainAction.isEnabled = true
                    binding.layoutQuickActions.visibility = View.GONE
                    binding.layoutBottomActions.visibility = View.GONE
                    binding.progressBar.visibility = View.GONE
                    binding.tvProgressText.visibility = View.GONE
                }
                LinuxSessionState.INSTALLING -> {
                    binding.viewStatusDot.background = getDrawable(R.drawable.status_dot_installing)
                    binding.tvStatusText.text = getString(R.string.status_installing)
                    binding.btnMainAction.text = getString(R.string.status_installing)
                    binding.btnMainAction.isEnabled = false
                    binding.layoutQuickActions.visibility = View.GONE
                    binding.layoutBottomActions.visibility = View.GONE
                    binding.progressBar.visibility = View.VISIBLE
                    binding.tvProgressText.visibility = View.VISIBLE
                    binding.progressBar.progress = (s.progress * 100).toInt()
                    binding.tvProgressText.text = s.currentOperation
                }
                LinuxSessionState.INSTALLED -> {
                    binding.viewStatusDot.background = getDrawable(R.drawable.status_dot_stopped)
                    binding.tvStatusText.text = getString(R.string.status_installed)
                    binding.btnMainAction.text = getString(R.string.btn_start_linux)
                    binding.btnMainAction.isEnabled = true
                    binding.layoutQuickActions.visibility = View.GONE
                    binding.layoutBottomActions.visibility = View.GONE
                    binding.progressBar.visibility = View.GONE
                    binding.tvProgressText.visibility = View.GONE
                }
                LinuxSessionState.STARTING -> {
                    binding.viewStatusDot.background = getDrawable(R.drawable.status_dot_starting)
                    binding.tvStatusText.text = getString(R.string.status_starting)
                    binding.btnMainAction.text = getString(R.string.status_starting)
                    binding.btnMainAction.isEnabled = false
                    binding.layoutQuickActions.visibility = View.GONE
                    binding.layoutBottomActions.visibility = View.GONE
                    binding.progressBar.visibility = View.VISIBLE
                    binding.tvProgressText.visibility = View.VISIBLE
                    binding.progressBar.progress = (s.progress * 100).toInt()
                    binding.tvProgressText.text = s.currentOperation
                }
                LinuxSessionState.RUNNING -> {
                    binding.viewStatusDot.background = getDrawable(R.drawable.status_dot_running)
                    binding.tvStatusText.text = getString(R.string.status_running)
                    binding.btnMainAction.text = getString(R.string.btn_open_desktop)
                    binding.btnMainAction.isEnabled = true
                    binding.layoutQuickActions.visibility = View.VISIBLE
                    binding.layoutBottomActions.visibility = View.VISIBLE
                    binding.progressBar.visibility = View.GONE
                    binding.tvProgressText.visibility = View.GONE
                    binding.cardError.visibility = View.GONE
                }
                LinuxSessionState.STOPPING -> {
                    binding.viewStatusDot.background = getDrawable(R.drawable.status_dot_starting)
                    binding.tvStatusText.text = "Stopping…"
                    binding.btnMainAction.isEnabled = false
                    binding.progressBar.visibility = View.VISIBLE
                    binding.tvProgressText.visibility = View.VISIBLE
                    binding.tvProgressText.text = "Stopping Linux…"
                }
                LinuxSessionState.ERROR -> {
                    binding.viewStatusDot.background = getDrawable(R.drawable.status_dot_error)
                    binding.tvStatusText.text = getString(R.string.status_error)
                    binding.btnMainAction.text = getString(R.string.btn_start_linux)
                    binding.btnMainAction.isEnabled = true
                    binding.layoutQuickActions.visibility = View.GONE
                    binding.layoutBottomActions.visibility = View.GONE
                    binding.progressBar.visibility = View.GONE
                    binding.tvProgressText.visibility = View.GONE
                    showError(s.errorMessage ?: getString(R.string.error_unknown))
                }
            }
        }
    }

    private fun showInstallDialog() {
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.app_name)
            .setMessage("This will download and install the Linux environment (several GB). Continue?")
            .setPositiveButton("Install") { _, _ ->
                startInstallation()
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun startInstallation() {
        sessionManager.installRootfs()
    }

    private fun showError(message: String) {
        binding.tvErrorMessage.text = message
        binding.cardError.visibility = View.VISIBLE
    }

    private fun openLinuxDesktop() {
        startActivity(Intent(this, DesktopActivity::class.java))
    }

    private fun openTerminal() {
        openLinuxDesktop()
    }

    private fun openFirefox() {
        openLinuxDesktop()
    }

    private fun openFiles() {
        openLinuxDesktop()
    }

    private fun openSettings() {
        lifecycleScope.launch {
            val autoStart = sessionManager.getAutoStart()
            val touchMode = sessionManager.getTouchMode()
            val labels = arrayOf(
                getString(R.string.settings_auto_start),
                getString(R.string.settings_touch_mode),
                getString(R.string.settings_diagnostics)
            )
            val checked = booleanArrayOf(autoStart, touchMode, false)
            MaterialAlertDialogBuilder(this@MainActivity)
                .setTitle(R.string.settings_title)
                .setMultiChoiceItems(labels, checked) { _, which, enabled ->
                    lifecycleScope.launch {
                        when (which) {
                            0 -> {
                                sessionManager.setAutoStart(enabled)
                                getSharedPreferences("linux_cyberdeck_prefs", MODE_PRIVATE)
                                    .edit().putBoolean("auto_start_enabled", enabled).apply()
                            }
                            1 -> sessionManager.setTouchMode(enabled)
                            2 -> if (enabled) openDiagnostics()
                        }
                    }
                }
                .setPositiveButton(android.R.string.ok, null)
                .show()
        }
    }

    private fun openDiagnostics() {
        lifecycleScope.launch {
            val diagnostics = sessionManager.getDiagnostics()
            diagnostics?.let { d ->
                runOnUiThread {
                    showDiagnosticsDialog(d)
                }
            } ?: runOnUiThread {
                Toast.makeText(this@MainActivity, "Failed to get diagnostics", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun showDiagnosticsDialog(diagnostics: LinuxNativeBridge.DiagnosticsNative) {
        val builder = StringBuilder()
        builder.appendLine("Android Version: ${diagnostics.androidVersion}")
        builder.appendLine("Device Model: ${diagnostics.deviceModel}")
        builder.appendLine("CPU Architecture: ${diagnostics.cpuArchitecture}")
        builder.appendLine("Available RAM: ${formatBytes(diagnostics.availableRamBytes)}")
        builder.appendLine("Available Storage: ${formatBytes(diagnostics.availableStorageBytes)}")
        builder.appendLine("")
        builder.appendLine("Linux Filesystem: ${diagnostics.linuxFilesystemStatus}")
        builder.appendLine("X11 Display Server: ${diagnostics.x11Status}")
        builder.appendLine("Debian Userspace: ${diagnostics.debianStatus}")
        builder.appendLine("XFCE Desktop: ${diagnostics.xfceStatus}")
        builder.appendLine("Firefox: ${diagnostics.firefoxStatus}")
        builder.appendLine("Storage Bridge: ${diagnostics.storageBridgeStatus}")
        builder.appendLine("Network: ${diagnostics.networkStatus}")

        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.diagnostics_title)
            .setMessage(builder.toString())
            .setPositiveButton("OK", null)
            .show()
    }

    companion object {
        private fun formatBytes(bytes: Long): String {
            return when {
                bytes >= 1024L * 1024 * 1024 -> String.format("%.1f GB", bytes / (1024.0 * 1024 * 1024))
                bytes >= 1024L * 1024 -> String.format("%.1f MB", bytes / (1024.0 * 1024))
                bytes >= 1024L -> String.format("%.1f KB", bytes / 1024.0)
                else -> "$bytes B"
            }
        }
    }
}
