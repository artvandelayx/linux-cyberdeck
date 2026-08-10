package com.linuxcyberdeck.receiver

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.os.Build
import com.linuxcyberdeck.service.LinuxSessionService
import timber.log.Timber

class BootReceiver : BroadcastReceiver() {

    companion object {
        const val PREFS_NAME = "linux_cyberdeck_prefs"
        const val KEY_AUTO_START = "auto_start_enabled"
    }

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action
        when (action) {
            Intent.ACTION_BOOT_COMPLETED,
            Intent.ACTION_LOCKED_BOOT_COMPLETED,
            "android.intent.action.QUICKBOOT_POWERON" -> {
                Timber.i("Boot completed received: $action")
                checkAndStartAutoStart(context)
            }
            else -> {
                Timber.w("Unknown boot action: $action")
            }
        }
    }

    private fun checkAndStartAutoStart(context: Context) {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val autoStart = prefs.getBoolean(KEY_AUTO_START, false)
        
        if (autoStart) {
            Timber.i("Auto-start enabled, starting Linux session service")
            val serviceIntent = Intent(context, LinuxSessionService::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(serviceIntent)
            } else {
                context.startService(serviceIntent)
            }
        } else {
            Timber.i("Auto-start disabled")
        }
    }
}