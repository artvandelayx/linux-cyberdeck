package com.linuxcyberdeck.session

import android.app.Application
import android.content.Context
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.linuxcyberdeck.LinuxCyberdeckApplication
import com.linuxcyberdeck.installer.LinuxInstaller
import com.linuxcyberdeck.native.LinuxNativeBridge
import com.linuxcyberdeck.native.LinuxNativeBridge.SessionStatusNative
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import timber.log.Timber
import android.content.Intent
import androidx.core.content.ContextCompat
import com.linuxcyberdeck.service.LinuxSessionService

class LinuxSessionManager(private val application: Application) : AndroidViewModel(application) {

    private val _sessionStatus = MutableLiveData<LinuxSessionStatus>()
    val sessionStatus: LiveData<LinuxSessionStatus> = _sessionStatus

    private val _isLoading = MutableLiveData<Boolean>()
    val isLoading: LiveData<Boolean> = _isLoading

    private val installer = LinuxInstaller(application)

    init {
        refreshStatus()
        // Observe installer status and forward to session status
        installer.installStatus.observeForever { installStatus ->
            if (installStatus.state == LinuxSessionState.INSTALLING || 
                installStatus.state == LinuxSessionState.INSTALLED ||
                installStatus.state == LinuxSessionState.ERROR) {
                _sessionStatus.postValue(LinuxSessionStatus(
                    state = installStatus.state,
                    progress = installStatus.progress,
                    currentOperation = installStatus.currentOperation,
                    errorMessage = installStatus.errorMessage,
                    availableStorageBytes = 0,
                    requiredStorageBytes = 0,
                    linuxFilesystemSizeBytes = 0,
                    x11Pid = 0,
                    xfcePid = 0,
                    debianPid = 0,
                    uptimeMillis = 0
                ))
                _isLoading.postValue(installStatus.state == LinuxSessionState.INSTALLING)
                if (installStatus.state == LinuxSessionState.INSTALLED) {
                    val prootPath = "${application.applicationInfo.nativeLibraryDir}/libproot.so"
                    LinuxNativeBridge.initializeSession(application.filesDir.absolutePath, application.packageName, prootPath)
                    refreshStatus()
                }
            }
        }
    }

    fun refreshStatus() {
        // refreshStatus is also called after native operations on Dispatchers.IO.
        // postValue is safe from both main and worker threads.
        _isLoading.postValue(true)
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val nativeStatus = LinuxNativeBridge.getSessionStatus()
                val status = convertFromNative(nativeStatus)
                _sessionStatus.postValue(status)
            } catch (e: Exception) {
                Timber.e(e, "Failed to get session status")
                _sessionStatus.postValue(LinuxSessionStatus(
                    state = LinuxSessionState.ERROR,
                    errorMessage = "Failed to communicate with native layer: ${e.message}"
                ))
            } finally {
                _isLoading.postValue(false)
            }
        }
    }

    fun startLinux() {
        _isLoading.value = true
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val result = LinuxNativeBridge.startLinuxSession()
                when (result) {
                    LinuxNativeBridge.RESULT_SUCCESS -> {
                        Timber.i("Linux session started successfully")
                        ContextCompat.startForegroundService(application, Intent(application, LinuxSessionService::class.java))
                        refreshStatus()
                    }
                    LinuxNativeBridge.RESULT_ALREADY_RUNNING -> {
                        Timber.w("Linux session already running")
                        _sessionStatus.postValue(_sessionStatus.value?.copy(
                            errorMessage = "Linux is already running"
                        ))
                    }
                    LinuxNativeBridge.RESULT_NOT_INSTALLED -> {
                        Timber.w("Linux not installed")
                        _sessionStatus.postValue(_sessionStatus.value?.copy(
                            state = LinuxSessionState.NOT_INSTALLED,
                            errorMessage = "Linux environment not installed. Please install first."
                        ))
                    }
                    else -> {
                        Timber.e("Failed to start Linux session: $result")
                        _sessionStatus.postValue(_sessionStatus.value?.copy(
                            state = LinuxSessionState.ERROR,
                            errorMessage = "Failed to start Linux session (code: $result)"
                        ))
                    }
                }
            } catch (e: Exception) {
                Timber.e(e, "Exception starting Linux session")
                _sessionStatus.postValue(_sessionStatus.value?.copy(
                    state = LinuxSessionState.ERROR,
                    errorMessage = "Exception: ${e.message}"
                ))
            } finally {
                _isLoading.postValue(false)
            }
        }
    }

    fun stopLinux() {
        _isLoading.value = true
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val result = LinuxNativeBridge.stopLinuxSession()
                when (result) {
                    LinuxNativeBridge.RESULT_SUCCESS -> {
                        Timber.i("Linux session stopped successfully")
                        application.stopService(Intent(application, LinuxSessionService::class.java))
                        refreshStatus()
                    }
                    else -> {
                        Timber.e("Failed to stop Linux session: $result")
                        _sessionStatus.postValue(_sessionStatus.value?.copy(
                            errorMessage = "Failed to stop Linux session (code: $result)"
                        ))
                    }
                }
            } catch (e: Exception) {
                Timber.e(e, "Exception stopping Linux session")
                _sessionStatus.postValue(_sessionStatus.value?.copy(
                    errorMessage = "Exception: ${e.message}"
                ))
            } finally {
                _isLoading.postValue(false)
            }
        }
    }

    fun restartLinux() {
        _isLoading.value = true
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val result = LinuxNativeBridge.restartLinuxSession()
                when (result) {
                    LinuxNativeBridge.RESULT_SUCCESS -> {
                        Timber.i("Linux session restarted successfully")
                        refreshStatus()
                    }
                    LinuxNativeBridge.RESULT_NOT_INSTALLED -> {
                        _sessionStatus.postValue(_sessionStatus.value?.copy(
                            state = LinuxSessionState.NOT_INSTALLED,
                            errorMessage = "Linux environment not installed"
                        ))
                    }
                    else -> {
                        Timber.e("Failed to restart Linux session: $result")
                        _sessionStatus.postValue(_sessionStatus.value?.copy(
                            errorMessage = "Failed to restart Linux session (code: $result)"
                        ))
                    }
                }
            } catch (e: Exception) {
                Timber.e(e, "Exception restarting Linux session")
                _sessionStatus.postValue(_sessionStatus.value?.copy(
                    errorMessage = "Exception: ${e.message}"
                ))
            } finally {
                _isLoading.postValue(false)
            }
        }
    }

    fun installRootfs() {
        _isLoading.value = true
        installer.installRootfs()
    }

    fun cancelInstallation() {
        installer.cancelInstallation()
        _isLoading.value = false
    }

    suspend fun getDiagnostics() = withContext(Dispatchers.IO) {
        try {
            LinuxNativeBridge.getDiagnostics()
        } catch (e: Exception) {
            Timber.e(e, "Failed to get diagnostics")
            null
        }
    }

    suspend fun getLogs(logType: Int, maxLines: Int) = withContext(Dispatchers.IO) {
        try {
            LinuxNativeBridge.getLogs(logType, maxLines)
        } catch (e: Exception) {
            Timber.e(e, "Failed to get logs")
            "Error: ${e.message}"
        }
    }

    suspend fun setAutoStart(enabled: Boolean) = withContext(Dispatchers.IO) {
        LinuxNativeBridge.setAutoStart(enabled)
    }

    suspend fun getAutoStart() = withContext(Dispatchers.IO) {
        LinuxNativeBridge.getAutoStart()
    }

    suspend fun setTouchMode(enabled: Boolean) = withContext(Dispatchers.IO) {
        LinuxNativeBridge.setTouchMode(enabled)
    }

    suspend fun getTouchMode() = withContext(Dispatchers.IO) {
        LinuxNativeBridge.getTouchMode()
    }

    private fun convertFromNative(native: SessionStatusNative): LinuxSessionStatus {
        val state = when (native.state) {
            LinuxNativeBridge.SESSION_STATE_NOT_INSTALLED -> LinuxSessionState.NOT_INSTALLED
            LinuxNativeBridge.SESSION_STATE_INSTALLING -> LinuxSessionState.INSTALLING
            LinuxNativeBridge.SESSION_STATE_INSTALLED -> LinuxSessionState.INSTALLED
            LinuxNativeBridge.SESSION_STATE_STARTING -> LinuxSessionState.STARTING
            LinuxNativeBridge.SESSION_STATE_RUNNING -> LinuxSessionState.RUNNING
            LinuxNativeBridge.SESSION_STATE_STOPPING -> LinuxSessionState.STOPPING
            LinuxNativeBridge.SESSION_STATE_ERROR -> LinuxSessionState.ERROR
            else -> LinuxSessionState.ERROR
        }
        
        return LinuxSessionStatus(
            state = state,
            progress = native.progress,
            currentOperation = native.currentOperation,
            errorMessage = native.errorMessage,
            availableStorageBytes = native.availableStorageBytes,
            requiredStorageBytes = native.requiredStorageBytes,
            linuxFilesystemSizeBytes = native.linuxFilesystemSizeBytes,
            x11Pid = native.x11Pid,
            xfcePid = native.xfcePid,
            debianPid = native.debianPid,
            uptimeMillis = native.uptimeMillis
        )
    }
}
