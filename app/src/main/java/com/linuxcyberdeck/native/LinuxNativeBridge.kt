package com.linuxcyberdeck.native

class LinuxNativeBridge {
    companion object {
        // Load the native library
        init {
            System.loadLibrary("linuxcyberdeck_jni")
        }

        // Session management
        @JvmStatic
        external fun initializeSession(appFilesDir: String, packageName: String): Int

        @JvmStatic
        external fun startLinuxSession(): Int

        @JvmStatic
        external fun stopLinuxSession(): Int

        @JvmStatic
        external fun restartLinuxSession(): Int

        @JvmStatic
        external fun getSessionStatus(): SessionStatusNative

        @JvmStatic
        external fun installRootfs(downloadUrl: String, expectedSize: Long): Int

        @JvmStatic
        external fun cancelInstallation(): Int

        // Storage bridge
        @JvmStatic
        external fun mountSharedStorage(androidPath: String, linuxMountPoint: String, storageManager: StorageBridgeManager): Int

        @JvmStatic
        external fun unmountSharedStorage(linuxMountPoint: String, storageManager: StorageBridgeManager): Int

        @JvmStatic
        external fun listMountedStorages(): Array<String>

        // Diagnostics
        @JvmStatic
        external fun getDiagnostics(): DiagnosticsNative

        @JvmStatic
        external fun getLogs(logType: Int, maxLines: Int): String

        // Configuration
        @JvmStatic
        external fun setAutoStart(enabled: Boolean): Int

        @JvmStatic
        external fun getAutoStart(): Boolean

        @JvmStatic
        external fun setTouchMode(enabled: Boolean): Int

        @JvmStatic
        external fun getTouchMode(): Boolean
    }

    data class SessionStatusNative(
        val state: Int,
        val progress: Float,
        val currentOperation: String,
        val errorMessage: String?,
        val availableStorageBytes: Long,
        val requiredStorageBytes: Long,
        val linuxFilesystemSizeBytes: Long,
        val x11Pid: Int,
        val xfcePid: Int,
        val debianPid: Int,
        val uptimeMillis: Long
    )

    data class DiagnosticsNative(
        val androidVersion: String,
        val deviceModel: String,
        val cpuArchitecture: String,
        val availableRamBytes: Long,
        val availableStorageBytes: Long,
        val linuxFilesystemStatus: String,
        val x11Status: String,
        val debianStatus: String,
        val xfceStatus: String,
        val firefoxStatus: String,
        val storageBridgeStatus: String,
        val networkStatus: String
    )

    companion object Constants {
        const val SESSION_STATE_NOT_INSTALLED = 0
        const val SESSION_STATE_INSTALLING = 1
        const val SESSION_STATE_INSTALLED = 2
        const val SESSION_STATE_STARTING = 3
        const val SESSION_STATE_RUNNING = 4
        const val SESSION_STATE_STOPPING = 5
        const val SESSION_STATE_ERROR = 6

        const val LOG_TYPE_X11 = 0
        const val LOG_TYPE_LINUX = 1
        const val LOG_TYPE_XFCE = 2
        const val LOG_TYPE_FIREFOX = 3
        const val LOG_TYPE_STORAGE = 4
        const val LOG_TYPE_APP = 5

        const val RESULT_SUCCESS = 0
        const val RESULT_ERROR = -1
        const val RESULT_ALREADY_RUNNING = -2
        const val RESULT_NOT_INSTALLED = -3
        const val RESULT_INSTALL_IN_PROGRESS = -4
        const val RESULT_INSUFFICIENT_STORAGE = -5
        const val RESULT_PERMISSION_DENIED = -6
    }
}