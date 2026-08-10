package com.linuxcyberdeck

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.util.Log
import android.view.View
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.lifecycle.Observer
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.snackbar.Snackbar
import com.linuxcyberdeck.databinding.ActivityMainBinding
import com.linuxcyberdeck.native.LinuxNativeBridge
import com.linuxcyberdeck.session.LinuxSessionManager
import com.linuxcyberdeck.session.LinuxSessionState
import com.linuxcyberdeck.session.LinuxSessionStatus
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import timber.log.Timber

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
        checkStoragePermissions()
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
        // Debian 13 ARM64 rootfs download URL (example - would be a real mirror)
        val downloadUrl = "https://deb.debian.org/debian/dists/trixie/main/installer-arm64/current/images/netboot/debian-installer/arm64/initrd.gz"
        val expectedSize = 4L * 1024 * 1024 * 1024 // 4 GB estimate
        sessionManager.installRootfs(downloadUrl, expectedSize)
    }

    private fun showError(message: String) {
        binding.tvErrorMessage.text = message
        binding.cardError.visibility = View.VISIBLE
    }

    private fun openLinuxDesktop() {
        // Launch XFCE desktop via the native layer
        // This would typically involve launching a VNC viewer or similar
        // For now, we'll show a toast and could launch an intent
        Snackbar.make(binding.root, "Opening Linux desktop…", Snackbar.LENGTH_SHORT).show()
        // TODO: Launch XFCE display (VNC/X11 forward)
    }

    private fun openTerminal() {
        Snackbar.make(binding.root, "Opening terminal…", Snackbar.LENGTH_SHORT).show()
        // TODO: Launch terminal (xfce4-terminal via X11)
    }

    private fun openFirefox() {
        Snackbar.make(binding.root, "Opening Firefox…", Snackbar.LENGTH_SHORT).show()
        // TODO: Launch Firefox via X11
    }

    private fun openFiles() {
        Snackbar.make(binding.root, "Opening file manager…", Snackbar.LENGTH_SHORT).show()
        // TODO: Launch Thunar via X11
    }

    private fun openSettings() {
        // TODO: Open settings activity/fragment
        Snackbar.make(binding.root, "Settings coming soon", Snackbar.LENGTH_SHORT).show()
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

    private fun checkStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                    intent.data = Uri.parse("package:$packageName")
                    startActivity(intent)
                } catch (e: Exception) {
                    Timber.w(e, "Could not open storage permission settings")
                }
            }
        }
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