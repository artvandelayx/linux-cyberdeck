package com.linuxcyberdeck

import android.app.Application
import android.content.Context
import android.util.Log
import timber.log.Timber

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
    }

    private class ReleaseTree : Timber.Tree() {
        override fun log(priority: Int, tag: String?, message: String, t: Throwable?) {
            if (priority >= Log.INFO) {
                Log.println(priority, tag ?: TAG, message)
            }
        }
    }
}