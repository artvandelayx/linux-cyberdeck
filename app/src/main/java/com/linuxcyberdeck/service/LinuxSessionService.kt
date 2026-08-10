package com.linuxcyberdeck.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import androidx.annotation.Nullable
import androidx.core.app.NotificationCompat
import com.linuxcyberdeck.MainActivity
import com.linuxcyberdeck.R
import com.linuxcyberdeck.native.LinuxNativeBridge
import timber.log.Timber

class LinuxSessionService : Service() {

    companion object {
        const val CHANNEL_ID = "linux_session_channel"
        const val NOTIFICATION_ID = 1001
        const val ACTION_STOP = "com.linuxcyberdeck.action.STOP"
        const val ACTION_RESTART = "com.linuxcyberdeck.action.RESTART"
    }

    private var wakeLock: PowerManager.WakeLock? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        acquireWakeLock()
        Timber.i("LinuxSessionService created")
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val action = intent?.action
        when (action) {
            ACTION_STOP -> {
                Timber.i("Stop action received")
                stopLinuxSession()
            }
            ACTION_RESTART -> {
                Timber.i("Restart action received")
                restartLinuxSession()
            }
            else -> {
                // Start the Linux session if not already running
                startLinuxSession()
            }
        }
        return START_STICKY
    }

    private fun startLinuxSession() {
        lifecycleScope.launch {
            val result = LinuxNativeBridge.startLinuxSession()
            when (result) {
                LinuxNativeBridge.RESULT_SUCCESS -> {
                    Timber.i("Linux session started from service")
                    updateNotification(R.string.status_running)
                }
                LinuxNativeBridge.RESULT_ALREADY_RUNNING -> {
                    Timber.w("Linux session already running")
                    updateNotification(R.string.status_running)
                }
                else -> {
                    Timber.e("Failed to start Linux session from service: $result")
                    updateNotificationError("Failed to start (code: $result)")
                }
            }
        }
    }

    private fun stopLinuxSession() {
        lifecycleScope.launch {
            val result = LinuxNativeBridge.stopLinuxSession()
            when (result) {
                LinuxNativeBridge.RESULT_SUCCESS -> {
                    Timber.i("Linux session stopped from service")
                    stopForeground(true)
                    stopSelf()
                }
                else -> {
                    Timber.e("Failed to stop Linux session from service: $result")
                    updateNotificationError("Failed to stop (code: $result)")
                }
            }
        }
    }

    private fun restartLinuxSession() {
        lifecycleScope.launch {
            val result = LinuxNativeBridge.restartLinuxSession()
            when (result) {
                LinuxNativeBridge.RESULT_SUCCESS -> {
                    Timber.i("Linux session restarted from service")
                    updateNotification(R.string.status_running)
                }
                LinuxNativeBridge.RESULT_NOT_INSTALLED -> {
                    Timber.w("Linux not installed")
                    updateNotificationError("Linux not installed")
                }
                else -> {
                    Timber.e("Failed to restart Linux session from service: $result")
                    updateNotificationError("Failed to restart (code: $result)")
                }
            }
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Linux Cyberdeck Session",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Keeps the Linux desktop running in the background"
                setShowBadge(false)
            }
            val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            manager.createNotificationChannel(channel)
        }
    }

    private fun acquireWakeLock() {
        val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
        wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "LinuxCyberdeck::WakeLock")
        wakeLock?.acquire()
    }

    private fun releaseWakeLock() {
        wakeLock?.release()
        wakeLock = null
    }

    private fun buildNotification(statusTextRes: Int): Notification {
        val stopIntent = Intent(this, LinuxSessionService::class.java).setAction(ACTION_STOP)
        val restartIntent = Intent(this, LinuxSessionService::class.java).setAction(ACTION_RESTART)
        val openIntent = Intent(this, MainActivity::class.java)

        val stopAction = NotificationCompat.Action(
            R.drawable.ic_stop,
            getString(R.string.btn_stop_linux),
            PendingIntent.getService(this, 0, stopIntent, PendingIntent.FLAG_IMMUTABLE)
        )

        val restartAction = NotificationCompat.Action(
            R.drawable.ic_restart,
            getString(R.string.btn_restart_linux),
            PendingIntent.getService(this, 1, restartIntent, PendingIntent.FLAG_IMMUTABLE)
        )

        val contentIntent = PendingIntent.getActivity(
            this, 0, openIntent, PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(getString(statusTextRes))
            .setContentIntent(contentIntent)
            .addAction(stopAction)
            .addAction(restartAction)
            .setOngoing(true)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setShowWhen(false)
            .build()
    }

    private fun buildErrorNotification(errorMessage: String): Notification {
        val openIntent = Intent(this, MainActivity::class.java)
        val contentIntent = PendingIntent.getActivity(
            this, 0, openIntent, PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_error)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(errorMessage)
            .setContentIntent(contentIntent)
            .setOngoing(true)
            .setCategory(NotificationCompat.CATEGORY_ERROR)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setShowWhen(false)
            .build()
    }

    private fun updateNotification(statusTextRes: Int) {
        val notification = buildNotification(statusTextRes)
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        manager.notify(NOTIFICATION_ID, notification)
    }

    private fun updateNotificationError(errorMessage: String) {
        val notification = buildErrorNotification(errorMessage)
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        manager.notify(NOTIFICATION_ID, notification)
    }

    override fun onDestroy() {
        releaseWakeLock()
        stopForeground(true)
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        manager.cancel(NOTIFICATION_ID)
        Timber.i("LinuxSessionService destroyed")
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null
}