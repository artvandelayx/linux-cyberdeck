package com.linuxcyberdeck

import android.app.Application
import android.content.Context
import android.util.Log
import timber.log.Timber
import com.linuxcyberdeck.native.LinuxNativeBridge
import java.io.File

class LinuxCyberdeckApplication : Application() {

    companion object {
        const val TAG = "LinuxCyberdeck"
        const val LINUX_FILES_DIR = "linux"
        const val LINUX_ROOTFS_DIR = "rootfs"
        const val LINUX_HOME_DIR = "home"
        const val LINUX_LOGS_DIR = "logs"
        const val LINUX_TMP_DIR = "tmp"
    }

    override fun onCreate() {
        super.onCreate()
        
        if (BuildConfig.DEBUG) {
            Timber.plant(Timber.DebugTree())
        } else {
            Timber.plant(ReleaseTree())
        }
        
        Timber.tag(TAG)
        Timber.i("Linux Cyberdeck Application started")
        // Refresh the launcher on every app update without reinstalling the
        // much larger Debian filesystem.
        val startScript = File(filesDir, "start_linux.sh")
        assets.open("start_linux.sh").use { input ->
            startScript.outputStream().use { output -> input.copyTo(output) }
        }
        check(startScript.setExecutable(true, true)) { "Could not make start_linux.sh executable" }

        val prootPath = "${applicationInfo.nativeLibraryDir}/libproot.so"
        val result = LinuxNativeBridge.initializeSession(filesDir.absolutePath, packageName, prootPath)
        if (result != LinuxNativeBridge.RESULT_SUCCESS) {
            Timber.e("Native session initialization failed: %d", result)
        }
    }

    private class ReleaseTree : Timber.Tree() {
        override fun log(priority: Int, tag: String?, message: String, t: Throwable?) {
            if (priority >= Log.INFO) {
                Log.println(priority, tag ?: TAG, message)
            }
        }
    }
}
