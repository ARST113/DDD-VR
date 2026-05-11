package top.rootu.dddvr.xr.activity

import android.app.Activity
import android.content.pm.ActivityInfo
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Surface
import android.view.WindowManager
import androidx.media3.common.Player
import top.rootu.dddvr.core.playback.PlaybackSession
import top.rootu.dddvr.model.MediaItem
import top.rootu.dddvr.player.PlayerManager
import top.rootu.dddvr.vr.activity.VrIntentParser
import top.rootu.dddvr.xr.bridge.OpenXrBridge
import top.rootu.dddvr.xr.model.OpenXrPlaybackConfig
import top.rootu.dddvr.xr.ui.OpenXrDebugOverlay

class OpenXrPlayerActivity : Activity(), OpenXrBridge.Callbacks {
    private lateinit var playerManager: PlayerManager
    private lateinit var playbackSession: PlaybackSession
    private lateinit var bridge: OpenXrBridge
    private val mainHandler = Handler(Looper.getMainLooper())
    private val startOpenXrRunnable = Runnable { startOpenXrFromResumedState() }
    private var activeSurface: Surface? = null
    private var initialized = false
    private var playerInitialized = false
    private var smokeOnly = false
    private var xrStartScheduled = false
    private var destroyed = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i("DDDVR/OpenXR", "ACTIVITY_ON_CREATE_BEGIN")
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val request = VrIntentParser.parse(intent) ?: run {
            Log.e("DDDVR/OpenXR", "Invalid intent for OpenXrPlayerActivity")
            finish()
            return
        }

        smokeOnly = intent?.getBooleanExtra("openxr_smoke_only", false) == true
        Log.i("DDDVR/OpenXR", "OpenXrPlayerActivity smokeOnly=$smokeOnly")
        if (!smokeOnly) {
            playerManager = PlayerManager(this, object : Player.Listener {})
            playbackSession = PlaybackSession(playerManager)
            playerInitialized = true
            playerManager.loadPlaylist(
                listOf(MediaItem(uri = request.uri, title = request.title, startPositionMs = request.startPositionMs)),
                0,
                request.startPositionMs
            )
        }

        val config = OpenXrPlaybackConfig.from(request)
        OpenXrDebugOverlay.logStartup(config)

        bridge = OpenXrBridge(this, this, config)
        Log.i("DDDVR/OpenXR", "ACTIVITY_ON_CREATE_END")
    }

    override fun onResume() {
        super.onResume()
        Log.i("DDDVR/OpenXR", "ACTIVITY_ON_RESUME initialized=$initialized scheduled=$xrStartScheduled")
        if (!::bridge.isInitialized) {
            Log.w("DDDVR/OpenXR", "ACTIVITY_ON_RESUME bridge not initialized")
            return
        }
        if (initialized) {
            runCatching { bridge.onResume() }
                .onFailure { Log.e("DDDVR/OpenXR", "Unable to resume OpenXR bridge", it) }
        } else {
            scheduleOpenXrStart()
        }
    }

    override fun onPause() {
        Log.i("DDDVR/OpenXR", "ACTIVITY_ON_PAUSE initialized=$initialized scheduled=$xrStartScheduled")
        mainHandler.removeCallbacks(startOpenXrRunnable)
        xrStartScheduled = false
        if (initialized) {
            runCatching { bridge.onPause() }
                .onFailure { Log.e("DDDVR/OpenXR", "Unable to pause OpenXR bridge", it) }
        }
        super.onPause()
    }

    override fun onDestroy() {
        Log.i("DDDVR/OpenXR", "ACTIVITY_ON_DESTROY initialized=$initialized scheduled=$xrStartScheduled")
        destroyed = true
        mainHandler.removeCallbacks(startOpenXrRunnable)
        xrStartScheduled = false
        if (initialized) {
            activeSurface?.let {
                if (!smokeOnly && playerInitialized) playbackSession.clearSurface(it)
                it.release()
            }
            runCatching { bridge.destroy() }
                .onFailure { Log.e("DDDVR/OpenXR", "Unable to destroy OpenXR bridge", it) }
        }
        if (playerInitialized) {
            playbackSession.release()
        }
        super.onDestroy()
    }

    override fun onVideoSurfaceReady(surface: Surface) {
        if (!smokeOnly && playerInitialized) activeSurface?.let { playbackSession.clearSurface(it) }
        activeSurface = surface
        if (!smokeOnly && playerInitialized) playbackSession.attachSurface(surface)
        OpenXrDebugOverlay.logSurfaceAttached(isAttached = true)
    }

    private fun scheduleOpenXrStart() {
        if (destroyed || !::bridge.isInitialized) return
        if (xrStartScheduled) {
            Log.i("DDDVR/OpenXR", "XR_START_ALREADY_SCHEDULED")
            return
        }
        xrStartScheduled = true
        Log.i("DDDVR/OpenXR", "XR_START_SCHEDULED delayMs=300")
        mainHandler.postDelayed(startOpenXrRunnable, XR_START_DELAY_MS)
    }

    private fun startOpenXrFromResumedState() {
        xrStartScheduled = false
        if (destroyed) {
            Log.w("DDDVR/OpenXR", "XR_START_SKIPPED destroyed=true")
            return
        }
        if (!::bridge.isInitialized) {
            Log.e("DDDVR/OpenXR", "XR_START_SKIPPED bridge not initialized")
            return
        }
        if (initialized) {
            Log.i("DDDVR/OpenXR", "XR_START_SKIPPED already initialized")
            runCatching { bridge.onResume() }
            return
        }

        Log.i("DDDVR/OpenXR", "XR_START_CALL_BEGIN")
        val startOk = runCatching { bridge.start() }
            .onFailure { Log.e("DDDVR/OpenXR", "Unable to start OpenXR bridge", it) }
            .getOrDefault(false)
        Log.i("DDDVR/OpenXR", "XR_START_CALL_END startOk=$startOk")
        Log.i("DDDVR/OpenXR", "NATIVE_START_RETURNED startOk=$startOk")
        if (!startOk) {
            Log.e("DDDVR/OpenXR", "OpenXR bridge start failed (null/invalid native handle)")
            finish()
            return
        }
        initialized = true
        runCatching { bridge.onResume() }
            .onFailure { Log.e("DDDVR/OpenXR", "Unable to resume OpenXR bridge after start", it) }
    }

    override fun onPlayPause() { if (!smokeOnly && playerInitialized) { if (playbackSession.isPlaying) playbackSession.pause() else playbackSession.play() } }
    override fun onSeekBy(deltaMs: Long) { if (!smokeOnly && playerInitialized) playbackSession.seekTo(playbackSession.currentPositionMs + deltaMs) }
    override fun onRecenter() { OpenXrDebugOverlay.logSessionState("recenter_request") }
    override fun onExit() { finish() }

    companion object {
        private const val XR_START_DELAY_MS = 300L
    }
}
