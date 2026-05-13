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
import top.rootu.dddvr.vr.activity.VrPlaybackRequest
import top.rootu.dddvr.xr.bridge.OpenXrBridge
import top.rootu.dddvr.xr.model.OpenXrPlaybackConfig
import top.rootu.dddvr.xr.ui.OpenXrDebugOverlay

class OpenXrPlayerActivity : Activity(), OpenXrBridge.Callbacks {
    private lateinit var playerManager: PlayerManager
    private lateinit var playbackSession: PlaybackSession
    private lateinit var bridge: OpenXrBridge
    private val mainHandler = Handler(Looper.getMainLooper())
    private val startOpenXrRunnable = Runnable { startOpenXrFromScheduledState() }
    private val playerStartRunnable = Runnable { initializePlayerIfNeeded("post_xr_start") }
    private var activeSurface: Surface? = null
    private var playbackRequest: VrPlaybackRequest? = null
    private var initialized = false
    private var playerInitialized = false
    private var smokeOnly = false
    private var xrStartScheduled = false
    private var playerStartScheduled = false
    private var destroyed = false
    private var resumed = false
    private var hasWindowFocus = false
    private var started = false
    private var stopped = false
    private var topResumed = false
    private var pausedBeforeXrStart = false
    private var xrStartAttempt = 0
    private var notResumedRetryCount = 0
    private var xrStartState = XrStartState.NOT_REQUESTED

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "ACTIVITY_ON_CREATE_BEGIN")
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val request = VrIntentParser.parse(intent) ?: run {
            Log.e(TAG, "Invalid intent for OpenXrPlayerActivity")
            finish()
            return
        }
        playbackRequest = request

        smokeOnly = intent?.getBooleanExtra("openxr_smoke_only", false) == true
        Log.i(TAG, "OpenXrPlayerActivity smokeOnly=$smokeOnly")

        val config = OpenXrPlaybackConfig.from(request)
        OpenXrDebugOverlay.logStartup(config)

        bridge = OpenXrBridge(this, this, config)
        Log.i(TAG, "ACTIVITY_ON_CREATE_END")
    }


    override fun onStart() {
        super.onStart()
        started = true
        stopped = false
        logLifecycle("ACTIVITY_ON_START")
    }

    override fun onRestart() {
        super.onRestart()
        logLifecycle("ACTIVITY_ON_RESTART")
    }

    override fun onStop() {
        logLifecycle("ACTIVITY_ON_STOP")
        stopped = true
        started = false
        topResumed = false
        super.onStop()
    }

    override fun onNewIntent(intent: android.content.Intent?) {
        super.onNewIntent(intent)
        setIntent(intent)
        logLifecycle("ACTIVITY_ON_NEW_INTENT")
    }

    override fun onUserLeaveHint() {
        super.onUserLeaveHint()
        logLifecycle("ACTIVITY_ON_USER_LEAVE_HINT")
    }

    override fun onTopResumedActivityChanged(isTopResumedActivity: Boolean) {
        super.onTopResumedActivityChanged(isTopResumedActivity)
        topResumed = isTopResumedActivity
        if (initialized && xrStartState == XrStartState.STARTED && !isTopResumedActivity) {
            Log.e(TAG, "CURRENT_BLOCKER ACTIVITY_LOST_TOP_RESUMED_AFTER_SWAPCHAIN")
        }
        logLifecycle("TOP_RESUMED_CHANGED isTopResumed=$isTopResumedActivity")
    }

    private fun logLifecycle(event: String) {
        Log.i(TAG, "$event initialized=$initialized playerInitialized=$playerInitialized smokeOnly=$smokeOnly xrStartState=$xrStartState xrStartScheduled=$xrStartScheduled resumed=$resumed topResumed=$topResumed hasWindowFocus=$hasWindowFocus started=$started destroyed=$destroyed stopped=$stopped pausedBeforeXrStart=$pausedBeforeXrStart isFinishing=$isFinishing isDestroyed=$isDestroyed attempt=$xrStartAttempt notResumedRetryCount=$notResumedRetryCount")
        Log.i(TAG, "XR_START_STATE event=$event xrStartState=$xrStartState")
    }

    override fun onResume() {
        super.onResume()
        resumed = true
        notResumedRetryCount = 0
        logLifecycle("ACTIVITY_ON_RESUME")
        if (!::bridge.isInitialized) {
            Log.w(TAG, "ACTIVITY_ON_RESUME bridge not initialized")
            return
        }
        if (initialized) {
            runCatching { bridge.onResume() }
                .onFailure { Log.e(TAG, "Unable to resume OpenXR bridge", it) }
            schedulePlayerStart("resume_initialized")
        } else {
            scheduleOpenXrStart("onResume_first", FIRST_XR_START_DELAY_MS)
        }
    }

    override fun onPostResume() {
        super.onPostResume()
        logLifecycle("ACTIVITY_ON_POST_RESUME")
        if (!initialized) {
            scheduleOpenXrStart("onPostResume", XR_RETRY_DELAY_MS)
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        hasWindowFocus = hasFocus
        logLifecycle("WINDOW_FOCUS_CHANGED hasFocus=$hasFocus")
        if (hasFocus && resumed && !initialized) {
            scheduleOpenXrStart("windowFocus", XR_RETRY_DELAY_MS)
        }
    }

    override fun onPause() {
        super.onPause()
        logLifecycle("ACTIVITY_ON_PAUSE")
        if (initialized && xrStartState == XrStartState.STARTED) {
            Log.e(TAG, "CURRENT_STATE SEETHROUGH_OR_GUARDIAN_STOLE_FOCUS")
            Log.e(TAG, "CURRENT_BLOCKER SEETHROUGH_SETTINGS_STOLE_FOCUS")
        }
        resumed = false
        hasWindowFocus = false

        if (!initialized && xrStartScheduled) {
            pausedBeforeXrStart = true
            Log.w(TAG, "XR_START_PAUSED_BEFORE_INIT keep pending start")
        }

        if (initialized) {
            mainHandler.removeCallbacks(startOpenXrRunnable)
            xrStartScheduled = false
            runCatching { bridge.onPause() }
                .onFailure { Log.e(TAG, "Unable to pause OpenXR bridge", it) }
        } else {
            // Keep the delayed OpenXR start runnable alive across Pico's very early shell/
            // boundary pause. The runnable will execute, observe resumed=false, and either
            // wait for a later resume/focus callback or emit an explicit blocker after
            // bounded retries. Cancelling here recreates the PR #41 failure: the log stops
            // at XR_START_PAUSED_BEFORE_INIT and native OpenXR is never called.
            Log.i(TAG, "XR_START_PENDING_ACROSS_EARLY_PAUSE scheduled=$xrStartScheduled")
        }

        mainHandler.removeCallbacks(playerStartRunnable)
        playerStartScheduled = false
        if (!isFinishing && !isDestroyed) {
            Log.i(TAG, "ACTIVITY_PAUSE_NON_FATAL keep OpenXR bridge alive")
        } else {
            Log.i(TAG, "ACTIVITY_PAUSE_FINISHING_OR_DESTROYING")
        }
    }

    override fun onDestroy() {
        logLifecycle("ACTIVITY_ON_DESTROY")
        destroyed = true
        resumed = false
        mainHandler.removeCallbacks(startOpenXrRunnable)
        mainHandler.removeCallbacks(playerStartRunnable)
        xrStartScheduled = false
        playerStartScheduled = false
        activeSurface?.let { surface ->
            if (!smokeOnly && playerInitialized) {
                runCatching { playbackSession.clearSurface(surface) }
                    .onFailure { Log.e(TAG, "Unable to clear playback surface", it) }
            }
            surface.release()
            activeSurface = null
        }
        if (::bridge.isInitialized) {
            runCatching { bridge.destroy() }
                .onFailure { Log.e(TAG, "Unable to destroy OpenXR bridge", it) }
        }
        if (playerInitialized) {
            playbackSession.release()
            playerInitialized = false
        }
        super.onDestroy()
    }

    override fun onVideoSurfaceReady(surface: Surface) {
        if (!smokeOnly && playerInitialized) activeSurface?.let { playbackSession.clearSurface(it) }
        activeSurface = surface
        if (!smokeOnly && playerInitialized) playbackSession.attachSurface(surface)
        OpenXrDebugOverlay.logSurfaceAttached(isAttached = true)
    }

    private fun scheduleOpenXrStart(reason: String, delayMs: Long) {
        if (destroyed || !::bridge.isInitialized) return
        if (initialized) {
            Log.i(TAG, "XR_START_SKIPPED already initialized reason=$reason")
            return
        }
        if (xrStartScheduled) {
            Log.i(TAG, "XR_START_ALREADY_SCHEDULED reason=$reason attempt=$xrStartAttempt")
            return
        }
        xrStartScheduled = true
        xrStartState = XrStartState.SCHEDULED
        Log.i(TAG, "XR_START_SCHEDULED reason=$reason delayMs=$delayMs attempt=${xrStartAttempt + 1}")
        if (delayMs == 0L) mainHandler.post(startOpenXrRunnable) else mainHandler.postDelayed(startOpenXrRunnable, delayMs)
    }

    private fun startOpenXrFromScheduledState() {
        xrStartScheduled = false
        if (destroyed) {
            Log.w(TAG, "XR_START_SKIPPED destroyed=true")
            return
        }
        if (!::bridge.isInitialized) {
            Log.e(TAG, "XR_START_SKIPPED bridge not initialized")
            return
        }
        if (initialized) {
            Log.i(TAG, "XR_START_SKIPPED already initialized")
            runCatching { bridge.onResume() }
                .onFailure { Log.e(TAG, "Unable to resume already initialized OpenXR bridge", it) }
            schedulePlayerStart("already_initialized")
            return
        }
        if (!resumed) {
            xrStartState = XrStartState.PAUSED_BEFORE_START
            notResumedRetryCount += 1
            Log.w(TAG, "XR_START_EXECUTE_SKIPPED_NOT_RESUMED retry=$notResumedRetryCount pausedBeforeStart=$pausedBeforeXrStart")
            if (notResumedRetryCount < MAX_NOT_RESUMED_RETRIES) {
                scheduleOpenXrStart("not_resumed_retry", XR_RETRY_DELAY_MS)
            } else {
                Log.e(TAG, "CURRENT_BLOCKER ACTIVITY_PAUSED_BEFORE_DELAYED_XR_START")
            }
            return
        }

        notResumedRetryCount = 0
        xrStartAttempt += 1
        xrStartState = XrStartState.STARTING
        Log.i(TAG, "XR_START_EXECUTE attempt=$xrStartAttempt pausedBeforeStart=$pausedBeforeXrStart focus=$hasWindowFocus")
        Log.i(TAG, "XR_START_CALL_BEGIN")
        val startOk = runCatching { bridge.start() }
            .onFailure { Log.e(TAG, "Unable to start OpenXR bridge", it) }
            .getOrDefault(false)
        Log.i(TAG, "XR_START_CALL_END startOk=$startOk")
        Log.i(TAG, "NATIVE_START_RETURNED startOk=$startOk")
        if (!startOk) {
            xrStartState = XrStartState.FAILED
            Log.e(TAG, "OpenXR bridge start failed (null/invalid native handle)")
            finish()
            return
        }
        initialized = true
        xrStartState = XrStartState.STARTED
        pausedBeforeXrStart = false
        runCatching { bridge.onResume() }
            .onFailure { Log.e(TAG, "Unable to resume OpenXR bridge after start", it) }
        schedulePlayerStart("xr_started")
    }

    private fun schedulePlayerStart(reason: String) {
        if (smokeOnly || playerInitialized || destroyed) return
        if (!initialized) {
            Log.i(TAG, "PLAYER_START_DELAYED reason=$reason initialized=false")
            return
        }
        if (playerStartScheduled) {
            Log.i(TAG, "PLAYER_START_ALREADY_SCHEDULED reason=$reason")
            return
        }
        playerStartScheduled = true
        Log.i(TAG, "PLAYER_START_SCHEDULED reason=$reason delayMs=$PLAYER_START_DELAY_MS")
        mainHandler.postDelayed(playerStartRunnable, PLAYER_START_DELAY_MS)
    }

    private fun initializePlayerIfNeeded(reason: String) {
        playerStartScheduled = false
        if (destroyed || smokeOnly || playerInitialized) return
        val request = playbackRequest ?: run {
            Log.e(TAG, "PLAYER_START_SKIPPED missing playback request")
            return
        }
        Log.i(TAG, "PLAYER_START_BEGIN reason=$reason")
        playerManager = PlayerManager(this, object : Player.Listener {})
        playbackSession = PlaybackSession(playerManager)
        playerInitialized = true
        playerManager.loadPlaylist(
            listOf(MediaItem(uri = request.uri, title = request.title, startPositionMs = request.startPositionMs)),
            0,
            request.startPositionMs
        )
        activeSurface?.let { playbackSession.attachSurface(it) }
        Log.i(TAG, "PLAYER_START_END")
    }

    enum class XrStartState {
        NOT_REQUESTED,
        SCHEDULED,
        STARTING,
        STARTED,
        PAUSED_BEFORE_START,
        FAILED
    }

    override fun onPlayPause() { if (!smokeOnly && playerInitialized) { if (playbackSession.isPlaying) playbackSession.pause() else playbackSession.play() } }
    override fun onSeekBy(deltaMs: Long) { if (!smokeOnly && playerInitialized) playbackSession.seekTo(playbackSession.currentPositionMs + deltaMs) }
    override fun onRecenter() { OpenXrDebugOverlay.logSessionState("recenter_request") }
    override fun onExit() { finish() }

    companion object {
        private const val TAG = "DDDVR/OpenXR"
        private const val FIRST_XR_START_DELAY_MS = 0L
        private const val XR_RETRY_DELAY_MS = 500L
        private const val PLAYER_START_DELAY_MS = 200L
        private const val MAX_NOT_RESUMED_RETRIES = 5
    }
}
