package com.linuxcyberdeck.session

enum class LinuxSessionState {
    NOT_INSTALLED,
    INSTALLING,
    INSTALLED,
    STARTING,
    RUNNING,
    STOPPING,
    ERROR
}

data class LinuxSessionStatus(
    val state: LinuxSessionState = LinuxSessionState.NOT_INSTALLED,
    val progress: Float = 0f,
    val currentOperation: String = "",
    val errorMessage: String? = null,
    val availableStorageBytes: Long = 0,
    val requiredStorageBytes: Long = 0,
    val linuxFilesystemSizeBytes: Long = 0,
    val x11Pid: Int = 0,
    val xfcePid: Int = 0,
    val debianPid: Int = 0,
    val uptimeMillis: Long = 0
) {
    val isRunning: Boolean
        get() = state == LinuxSessionState.RUNNING
    
    val isTerminalState: Boolean
        get() = state == LinuxSessionState.RUNNING || state == LinuxSessionState.ERROR || state == LinuxSessionState.NOT_INSTALLED
    
    val canStart: Boolean
        get() = state == LinuxSessionState.INSTALLED || state == LinuxSessionState.ERROR || state == LinuxSessionState.NOT_INSTALLED
    
    val canStop: Boolean
        get() = state == LinuxSessionState.RUNNING || state == LinuxSessionState.STARTING
    
    val canRestart: Boolean
        get() = state == LinuxSessionState.RUNNING || state == LinuxSessionState.ERROR
}