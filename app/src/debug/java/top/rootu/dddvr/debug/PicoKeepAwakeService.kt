package top.rootu.dddvr.debug

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import top.rootu.dddvr.R

class PicoKeepAwakeService : Service() {
    private lateinit var powerManager: PowerManager
    private val handler = Handler(Looper.getMainLooper())
    private var wakeLock: PowerManager.WakeLock? = null

    private val wakeCheck = object : Runnable {
        override fun run() {
            if (!powerManager.isInteractive) {
                wakeLock?.takeIf { it.isHeld }?.release()
                acquireWakeLock()
            }
            handler.postDelayed(this, WAKE_CHECK_INTERVAL_MS)
        }
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, buildNotification())

        powerManager = getSystemService(PowerManager::class.java)
        acquireWakeLock()
        handler.post(wakeCheck)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            stopSelf()
            return START_NOT_STICKY
        }
        return START_STICKY
    }

    override fun onDestroy() {
        handler.removeCallbacks(wakeCheck)
        wakeLock?.takeIf { it.isHeld }?.release()
        wakeLock = null
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    @Suppress("DEPRECATION")
    private fun acquireWakeLock() {
        if (wakeLock == null) {
            wakeLock = powerManager.newWakeLock(
                PowerManager.SCREEN_BRIGHT_WAKE_LOCK or
                    PowerManager.ACQUIRE_CAUSES_WAKEUP or
                    PowerManager.ON_AFTER_RELEASE,
                "$packageName:pico-test-keep-awake"
            ).apply {
                setReferenceCounted(false)
            }
        }
        wakeLock?.takeUnless { it.isHeld }?.acquire()
    }

    private fun createNotificationChannel() {
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(
                CHANNEL_ID,
                "Pico test runner",
                NotificationManager.IMPORTANCE_LOW
            )
        )
    }

    private fun buildNotification(): Notification =
        Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.mipmap.ic_launcher)
            .setContentTitle("DDD VR test runner")
            .setContentText("Keeping Pico awake for screenshot comparison")
            .setOngoing(true)
            .build()

    companion object {
        const val ACTION_STOP = "top.rootu.dddvr.debug.STOP_KEEP_AWAKE"
        private const val CHANNEL_ID = "pico_test_runner"
        private const val NOTIFICATION_ID = 991
        private const val WAKE_CHECK_INTERVAL_MS = 2_000L
    }
}
