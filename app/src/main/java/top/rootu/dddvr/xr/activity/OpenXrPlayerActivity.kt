package top.rootu.dddvr.xr.activity

import android.app.Activity
import android.content.Intent
import android.content.pm.ActivityInfo
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.KeyEvent
import android.view.Surface
import android.view.WindowManager
import androidx.media3.common.Format
import androidx.media3.common.Player
import androidx.media3.common.PlaybackException
import androidx.media3.common.Tracks
import top.rootu.dddvr.core.playback.PlaybackSession
import top.rootu.dddvr.logic.TrackLogic
import top.rootu.dddvr.model.MediaItem
import top.rootu.dddvr.player.PlayerManager
import top.rootu.dddvr.utils.MediaFormatHelper
import top.rootu.dddvr.viewmodel.TrackOption
import top.rootu.dddvr.vr.activity.VrIntentParser
import top.rootu.dddvr.vr.activity.VrPlaybackRequest
import top.rootu.dddvr.vr.input.VrControllerInputMapper
import top.rootu.dddvr.vr.input.VrKeyAction
import top.rootu.dddvr.vr.stereo.StereoInputMode
import top.rootu.dddvr.xr.bridge.OpenXrBridge
import top.rootu.dddvr.xr.model.OpenXrPlaybackConfig
import top.rootu.dddvr.xr.model.OpenXrScreenMode
import top.rootu.dddvr.xr.ui.OpenXrDebugOverlay
import top.rootu.dddvr.xr.ui.OpenXrAudioUiState
import top.rootu.dddvr.xr.ui.OpenXrDisplayUiState
import top.rootu.dddvr.xr.ui.OpenXrPlayerUiAction
import top.rootu.dddvr.xr.ui.OpenXrPlayerUiState
import top.rootu.dddvr.xr.ui.OpenXrPlaylistRow
import top.rootu.dddvr.xr.ui.OpenXrSubtitlesUiState

class OpenXrPlayerActivity : Activity(), OpenXrBridge.Callbacks {
    private lateinit var playerManager: PlayerManager
    private lateinit var playbackSession: PlaybackSession
    private lateinit var bridge: OpenXrBridge
    private val mainHandler = Handler(Looper.getMainLooper())
    private val startOpenXrRunnable = Runnable { startOpenXrFromScheduledState() }
    private val playerStartRunnable = Runnable { initializePlayerIfNeeded("post_xr_start") }
    private val uiStateRunnable = object : Runnable {
        override fun run() {
            updateOpenXrUiState("tick")
            if (!destroyed) mainHandler.postDelayed(this, UI_STATE_UPDATE_MS)
        }
    }
    private var activeSurface: Surface? = null
    private var playbackRequest: VrPlaybackRequest? = null
    private lateinit var playbackConfig: OpenXrPlaybackConfig
    private var audioOptions: List<TrackOption> = emptyList()
    private var selectedAudioTrackIndex = 0
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
    private var resumePlaybackAfterFocusLoss = false
    private var openXrUiVisible = true
    private var activeModal = OpenXrModal.NONE
    private var activeSettingsTab = OpenXrSettingsTab.DISPLAY
    private var muted = false
    private var aspectRatio = "Оригинал"
    private var playbackSpeed = 1.0f
    private var enhanceVideo = false
    private var currentVideoIsHdr = false
    private var currentVideoUseFfmpeg = false
    private var ffmpegVideoPipelineEnabled = false
    private var ffmpegVideoStartAttempt = 0
    private var lastNativeVideoWidth = 0
    private var lastNativeVideoHeight = 0
    private var spatialAudio = false
    private var subtitlesEnabled = false
    private var xrStartAttempt = 0
    private var notResumedRetryCount = 0
    private var sourceErrorRetryCount = 0
    private var xrStartState = XrStartState.NOT_REQUESTED

    private val openXrPlayerListener = object : Player.Listener {
        override fun onPlaybackStateChanged(playbackState: Int) {
            if (playbackState == Player.STATE_READY) {
                sourceErrorRetryCount = 0
            }
            updateOpenXrUiState("player_state_$playbackState")
        }

        override fun onIsPlayingChanged(isPlaying: Boolean) {
            updateOpenXrUiState("is_playing_$isPlaying")
        }

        override fun onTracksChanged(tracks: Tracks) {
            updateAudioTrackOptions("tracks_changed")
        }

        override fun onPlayerError(error: PlaybackException) {
            if (recoverFromSourceError(error)) return
            Log.e(TAG, "XR_PLAYER_FATAL code=${error.errorCodeName} message=${error.message}", error)
            openXrUiVisible = true
            updateOpenXrUiState("player_fatal_error")
        }
    }

    private fun configureWakeForOpenXrLaunch() {
        window.addFlags(
            WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON or
                WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON or
                WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED
        )
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            window.colorMode = ActivityInfo.COLOR_MODE_WIDE_COLOR_GAMUT
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
            setTurnScreenOn(true)
            setShowWhenLocked(true)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "ACTIVITY_ON_CREATE_BEGIN")
        configureWakeForOpenXrLaunch()

        val parsedRequest = VrIntentParser.parse(intent)
        val request = parsedRequest ?: restoreLastPlaybackRequest()
        playbackRequest = request

        smokeOnly = intent?.getBooleanExtra("openxr_smoke_only", false) == true || request == null
        ffmpegVideoPipelineEnabled = !smokeOnly
        if (intent?.getBooleanExtra("dddvr_debug_open_audio_panel", false) == true) {
            activeModal = OpenXrModal.SETTINGS
            activeSettingsTab = OpenXrSettingsTab.AUDIO
            openXrUiVisible = true
            Log.i(TAG, "XR_DEBUG_OPEN_AUDIO_PANEL")
        }
        Log.i(TAG, "OpenXrPlayerActivity smokeOnly=$smokeOnly")

        if (parsedRequest != null) {
            saveLastPlaybackRequest(parsedRequest)
        }

        playbackConfig = request?.let { OpenXrPlaybackConfig.from(it) }
            ?: OpenXrPlaybackConfig(
                stereoMode = StereoInputMode.MONO,
                swapEyes = false,
                screenMode = OpenXrScreenMode.FLAT,
                startPositionMs = 0L
            )
        OpenXrDebugOverlay.logStartup(playbackConfig)

        bridge = OpenXrBridge(this, this, playbackConfig)
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
        if (initialized || playerInitialized) {
            mainHandler.post { recreate() }
        }
    }

    override fun onUserLeaveHint() {
        super.onUserLeaveHint()
        logLifecycle("ACTIVITY_ON_USER_LEAVE_HINT")
    }

    override fun onTopResumedActivityChanged(isTopResumedActivity: Boolean) {
        super.onTopResumedActivityChanged(isTopResumedActivity)
        topResumed = isTopResumedActivity
        if (initialized && xrStartState == XrStartState.STARTED && !isTopResumedActivity) {
            Log.w(TAG, "XR_FOCUS_TRANSIENT ACTIVITY_LOST_TOP_RESUMED_AFTER_SWAPCHAIN")
        }
        if (isTopResumedActivity) {
            if (initialized && !playerInitialized) {
                schedulePlayerStart("top_resumed")
            }
            resumePlaybackIfForeground("top_resumed")
        }
        logLifecycle("TOP_RESUMED_CHANGED isTopResumed=$isTopResumedActivity")
    }

    private fun logLifecycle(event: String) {
        Log.i(TAG, "$event initialized=$initialized playerInitialized=$playerInitialized smokeOnly=$smokeOnly xrStartState=$xrStartState xrStartScheduled=$xrStartScheduled resumed=$resumed topResumed=$topResumed hasWindowFocus=$hasWindowFocus started=$started destroyed=$destroyed stopped=$stopped pausedBeforeXrStart=$pausedBeforeXrStart resumePlaybackAfterFocusLoss=$resumePlaybackAfterFocusLoss isFinishing=$isFinishing isDestroyed=$isDestroyed attempt=$xrStartAttempt notResumedRetryCount=$notResumedRetryCount")
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
        resumePlaybackIfForeground("activity_resume")
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
        if (hasFocus) {
            if (initialized && !playerInitialized) {
                schedulePlayerStart("window_focus")
            }
            resumePlaybackIfForeground("window_focus")
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN) return super.dispatchKeyEvent(event)
        val action = VrControllerInputMapper.map(event.keyCode)
        if (action == VrKeyAction.NONE) return super.dispatchKeyEvent(event)

        Log.i(TAG, "XR_KEY_ACTION key=${event.keyCode} action=$action")
        return when (action) {
            VrKeyAction.PLAY_PAUSE -> {
                onPlayPause()
                true
            }
            VrKeyAction.PLAY -> {
                if (!smokeOnly && playerInitialized) {
                    val state = bridge.getFfmpegPlaybackState()
                    bridge.setFfmpegPlaybackState(true, state.positionMs)
                    openXrUiVisible = true
                    updateOpenXrUiState("key_play")
                }
                true
            }
            VrKeyAction.PAUSE -> {
                if (!smokeOnly && playerInitialized) {
                    val state = bridge.getFfmpegPlaybackState()
                    bridge.setFfmpegPlaybackState(false, state.positionMs)
                    openXrUiVisible = true
                    updateOpenXrUiState("key_pause")
                }
                true
            }
            VrKeyAction.SEEK_BACK -> {
                onSeekBy(-15_000)
                true
            }
            VrKeyAction.SEEK_FORWARD -> {
                onSeekBy(15_000)
                true
            }
            VrKeyAction.TOGGLE_OVERLAY -> {
                onShowMenu()
                true
            }
            VrKeyAction.HIDE_OR_EXIT -> {
                if (openXrUiVisible) {
                    openXrUiVisible = false
                    updateOpenXrUiState("key_hide_ui")
                } else {
                    finish()
                }
                true
            }
            VrKeyAction.RECENTER -> {
                onRecenter()
                true
            }
            VrKeyAction.TOGGLE_STEREO -> {
                openXrUiVisible = true
                updateOpenXrUiState("key_stereo_request")
                OpenXrDebugOverlay.logSessionState("stereo_toggle_request")
                true
            }
            VrKeyAction.NONE -> false
        }
    }

    override fun onPause() {
        super.onPause()
        logLifecycle("ACTIVITY_ON_PAUSE")
        if (initialized && xrStartState == XrStartState.STARTED) {
            Log.w(TAG, "CURRENT_STATE XR_FOCUS_OR_GUARDIAN_PAUSE")
            Log.w(TAG, "XR_FOCUS_TRANSIENT SEETHROUGH_OR_GUARDIAN_STOLE_FOCUS")
        }
        pausePlaybackForFocusLoss("activity_pause")
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
        mainHandler.removeCallbacks(uiStateRunnable)
        xrStartScheduled = false
        playerStartScheduled = false
        activeSurface?.let { surface ->
            activeSurface = null
        }
        if (::bridge.isInitialized) {
            if (currentVideoUseFfmpeg) {
                runCatching { bridge.stopFfmpegVideoSource() }
                    .onFailure { Log.e(TAG, "Unable to stop FFmpeg video source", it) }
            }
            runCatching { bridge.destroy() }
                .onFailure { Log.e(TAG, "Unable to destroy OpenXR bridge", it) }
        }
        currentVideoUseFfmpeg = false
        if (playerInitialized) {
            playerInitialized = false
        }
        super.onDestroy()
    }

    override fun onVideoSurfaceReady(surface: Surface) {
        activeSurface = surface
        Log.i(TAG, "XR_VIDEO_SURFACE_HELD_FOR_FFMPEG surface=$surface playerInitialized=$playerInitialized")
        Log.i(TAG, "XR_VIDEO_SURFACE_READY surface=$surface playerInitialized=$playerInitialized ffmpegVideo=$ffmpegVideoPipelineEnabled")
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
        if (delayMs == 0L) startOpenXrFromScheduledState() else mainHandler.postDelayed(startOpenXrRunnable, delayMs)
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
        updateOpenXrUiState("xr_started")
        mainHandler.removeCallbacks(uiStateRunnable)
        mainHandler.post(uiStateRunnable)
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
        if (!isForegroundForPlayback()) {
            Log.i(TAG, "PLAYER_START_DEFERRED_NOT_FOREGROUND reason=$reason resumed=$resumed topResumed=$topResumed hasWindowFocus=$hasWindowFocus")
            return
        }
        val request = playbackRequest ?: run {
            Log.e(TAG, "PLAYER_START_SKIPPED missing playback request")
            return
        }
        Log.i(TAG, "PLAYER_START_BEGIN reason=$reason")
        ffmpegVideoPipelineEnabled = true
        playerInitialized = true
        requestFfmpegVideoStart(
            request = request,
            startMs = request.startPositionMs,
            reason = "player_start",
            forceRestart = false
        )
        updateAudioTrackOptions("player_start")
        updateOpenXrUiState("player_start_end")
        Log.i(TAG, "PLAYER_START_END backend=ffmpeg_only exoCreated=false")
    }

    private fun recoverFromSourceError(error: PlaybackException): Boolean {
        if (!::playerManager.isInitialized || !playerInitialized) return false
        val player = playerManager.exoPlayer ?: return false
        if (player.mediaItemCount <= 0) return false
        if (sourceErrorRetryCount >= MAX_SOURCE_ERROR_RETRIES) return false

        sourceErrorRetryCount += 1
        val retryIndex = player.currentMediaItemIndex.coerceIn(0, player.mediaItemCount - 1)
        val retryPosition = player.currentPosition.coerceAtLeast(0L)
        val items = (0 until player.mediaItemCount).map { index -> player.getMediaItemAt(index) }
        val delayMs = SOURCE_ERROR_RETRY_DELAY_MS * sourceErrorRetryCount

        Log.w(
            TAG,
            "XR_SOURCE_ERROR_RECOVER attempt=$sourceErrorRetryCount/$MAX_SOURCE_ERROR_RETRIES " +
                "delayMs=$delayMs pos=$retryPosition code=${error.errorCodeName} cause=${rootCauseMessage(error)}"
        )
        openXrUiVisible = true
        updateOpenXrUiState("source_error_recover")

        mainHandler.postDelayed({
            if (destroyed || !playerInitialized || smokeOnly) return@postDelayed
            val retryPlayer = playerManager.exoPlayer ?: return@postDelayed
            runCatching {
                retryPlayer.setMediaItems(items, retryIndex, retryPosition)
                retryPlayer.prepare()
                retryPlayer.playWhenReady = true
                retryPlayer.play()
                if (ffmpegVideoPipelineEnabled) {
                    playerManager.setVideoTrackDisabled(true, "source_error_retry")
                } else {
                    activeSurface?.let { playbackSession.attachSurface(it) }
                }
                updateOpenXrUiState("source_error_retry")
                Log.i(TAG, "XR_SOURCE_ERROR_RETRY_START attempt=$sourceErrorRetryCount pos=$retryPosition")
            }.onFailure {
                Log.e(TAG, "XR_SOURCE_ERROR_RETRY_FAILED attempt=$sourceErrorRetryCount", it)
            }
        }, delayMs)
        return true
    }

    private fun rootCauseMessage(error: Throwable): String {
        var cause: Throwable = error
        while (cause.cause != null && cause.cause !== cause) {
            cause = cause.cause!!
        }
        return "${cause.javaClass.simpleName}:${cause.message}"
    }

    private fun pausePlaybackForFocusLoss(reason: String) {
        if (smokeOnly || !playerInitialized) return
        val state = bridge.getFfmpegPlaybackState()
        if (state.playing) {
            resumePlaybackAfterFocusLoss = true
            bridge.setFfmpegPlaybackState(false, state.positionMs)
            updateOpenXrUiState("focus_loss_pause")
            Log.i(TAG, "PLAYER_PAUSED_FOR_XR_FOCUS_LOSS reason=$reason position=${state.positionMs}")
        } else {
            resumePlaybackAfterFocusLoss = false
            Log.i(TAG, "PLAYER_PAUSE_FOR_XR_FOCUS_LOSS_SKIPPED notPlaying reason=$reason")
        }
    }

    private fun isForegroundForPlayback(): Boolean {
        // Pico/OpenXR activities can render after Android reports no top/focus window.
        // Once the OpenXR session is started, resumed is the reliable playback gate.
        return resumed && (topResumed || hasWindowFocus || initialized)
    }

    private fun resumePlaybackIfForeground(reason: String) {
        if (smokeOnly || !playerInitialized || !resumePlaybackAfterFocusLoss) return
        if (!isForegroundForPlayback()) {
            Log.i(TAG, "PLAYER_RESUME_AFTER_XR_FOCUS_LOSS_DEFERRED reason=$reason resumed=$resumed topResumed=$topResumed hasWindowFocus=$hasWindowFocus")
            return
        }
        resumePlaybackAfterFocusLoss = false
        val state = bridge.getFfmpegPlaybackState()
        bridge.setFfmpegPlaybackState(true, state.positionMs)
        updateOpenXrUiState("focus_return_play")
        Log.i(TAG, "PLAYER_RESUMED_AFTER_XR_FOCUS_LOSS reason=$reason position=${state.positionMs}")
    }

    private fun onVideoFormatChanged(format: Format) {
        val width = format.width
        val height = format.height
        if (width <= 0 || height <= 0) return
        val hdrInfo = MediaFormatHelper.getHdrInfo(format)
        currentVideoIsHdr = hdrInfo.isNotBlank()
        val hdr = hdrInfo.ifBlank { "SDR" }
        val codec = MediaFormatHelper.getShortVideoCodecName(format).ifBlank { format.sampleMimeType.orEmpty() }
        Log.i(
            TAG,
            "XR_VIDEO_FORMAT width=$width height=$height codec=$codec hdr=$hdr autoEnhance=$currentVideoIsHdr sampleMime=${format.sampleMimeType} codecs=${format.codecs} colorInfo=${format.colorInfo} bitrate=${format.bitrate}"
        )
        bridge.setVideoSize(width, height)
        updateFfmpegVideoBackend(format, width, height, "video_format")
        updateOpenXrUiState("video_format")
    }

    private fun updateFfmpegVideoBackend(format: Format, width: Int, height: Int, reason: String) {
        if (smokeOnly || !playerInitialized || !::bridge.isInitialized) return
        val request = playbackRequest ?: return
        val sampleMime = format.sampleMimeType.orEmpty()
        val codecs = format.codecs.orEmpty()
        val isDolbyVision = sampleMime == androidx.media3.common.MimeTypes.VIDEO_DOLBY_VISION ||
            codecs.startsWith("dvh1", ignoreCase = true) ||
            codecs.startsWith("dvhe", ignoreCase = true)
        val isUhd = width >= 3840 || height >= 2160
        val wouldUseSoftwareFfmpeg =
            currentVideoIsHdr || isDolbyVision || isUhd || requestLikelyNeedsFfmpeg(request)
        val shouldUseFfmpeg = ffmpegVideoPipelineEnabled
        if (shouldUseFfmpeg) {
            playerManager.setVideoTrackDisabled(true, "video_format_ffmpeg")
            val startMs = if (::playbackSession.isInitialized) {
                playbackSession.currentPositionMs
            } else {
                request.startPositionMs
            }.coerceAtLeast(0L)
            requestFfmpegVideoStart(
                request = request,
                startMs = startMs,
                reason = reason,
                forceRestart = false
            )
            Log.i(
                TAG,
                "XR_FFMPEG_VIDEO_BACKEND_REQUEST reason=$reason width=$width height=$height " +
                    "hdr=$currentVideoIsHdr dovi=$isDolbyVision uhd=$isUhd heuristic=$wouldUseSoftwareFfmpeg " +
                    "startMs=$startMs uri=${request.uri}"
            )
        } else if (currentVideoUseFfmpeg) {
            currentVideoUseFfmpeg = false
            ffmpegVideoStartAttempt = 0
            bridge.stopFfmpegVideoSource()
            Log.i(TAG, "XR_FFMPEG_VIDEO_BACKEND_STOP reason=$reason width=$width height=$height")
        } else if (wouldUseSoftwareFfmpeg) {
            Log.i(
                TAG,
                "XR_FFMPEG_VIDEO_BACKEND_SKIPPED reason=$reason ffmpegPipeline=false " +
                    "width=$width height=$height hdr=$currentVideoIsHdr dovi=$isDolbyVision uhd=$isUhd " +
                    "usingHardwarePlayer=true"
            )
        }
    }

    private fun startFfmpegVideoBackendFromRequestIfNeeded(request: VrPlaybackRequest, reason: String) {
        if (smokeOnly || !playerInitialized || !::bridge.isInitialized || currentVideoUseFfmpeg) return
        if (!ffmpegVideoPipelineEnabled) return
        if (!requestLikelyNeedsFfmpeg(request)) return

        val startMs = request.startPositionMs.coerceAtLeast(0L)
        currentVideoIsHdr = currentVideoIsHdr || requestLooksHdr(request)
        requestFfmpegVideoStart(
            request = request,
            startMs = startMs,
            reason = reason,
            forceRestart = false
        )
        Log.i(
            TAG,
            "XR_FFMPEG_VIDEO_BACKEND_REQUEST reason=$reason source=request_heuristic " +
                "hdrByName=${requestLooksHdr(request)} startMs=$startMs uri=${request.uri}"
        )
    }

    private fun requestFfmpegVideoStart(
        request: VrPlaybackRequest,
        startMs: Long,
        reason: String,
        forceRestart: Boolean
    ) {
        if (smokeOnly || !playerInitialized || !::bridge.isInitialized) return
        if (currentVideoUseFfmpeg && !forceRestart) {
            syncFfmpegVideoPlayback("refresh_$reason")
            return
        }

        currentVideoUseFfmpeg = true
        ffmpegVideoStartAttempt += 1
        bridge.startFfmpegVideoSource(request.uri.toString(), startMs.coerceAtLeast(0L))
        bridge.setFfmpegPlaybackState(true, startMs.coerceAtLeast(0L))
        Log.i(
            TAG,
            "XR_FFMPEG_VIDEO_START_REQUEST reason=$reason forceRestart=$forceRestart " +
                "attempt=$ffmpegVideoStartAttempt startMs=$startMs uri=${request.uri}"
        )

        if (!forceRestart && ffmpegVideoStartAttempt < MAX_FFMPEG_VIDEO_START_ATTEMPTS) {
            mainHandler.postDelayed({
                if (destroyed || smokeOnly || !playerInitialized || !currentVideoUseFfmpeg) return@postDelayed
                syncFfmpegVideoPlayback("retry_sync_$reason")
            }, FFMPEG_VIDEO_START_RETRY_DELAY_MS)
        }
    }

    private fun requestLooksHdr(request: VrPlaybackRequest): Boolean {
        val marker = "${request.uri} ${request.title.orEmpty()}".lowercase()
        return marker.contains("hdr") ||
            marker.contains("hdr10") ||
            marker.contains("dolby") ||
            marker.contains("dovi") ||
            marker.contains("dv.")
    }

    private fun requestLikelyNeedsFfmpeg(request: VrPlaybackRequest): Boolean {
        val marker = "${request.uri} ${request.title.orEmpty()}".lowercase()
        return requestLooksHdr(request) ||
            marker.contains("2160p") ||
            marker.contains("uhd") ||
            marker.contains("4k") ||
            marker.contains("x265") ||
            marker.contains("h265") ||
            marker.contains("hevc")
    }

    private fun syncFfmpegVideoPlayback(reason: String, forceSeek: Boolean = false) {
        if (!currentVideoUseFfmpeg || smokeOnly || !playerInitialized || !::bridge.isInitialized) {
            return
        }
        val state = bridge.getFfmpegPlaybackState()
        if (forceSeek) {
            bridge.setFfmpegPlaybackState(state.playing, state.positionMs, forceSeek = true)
        }
        if (reason != "tick") {
            Log.i(
                TAG,
                "XR_FFMPEG_VIDEO_SYNC reason=$reason playing=${state.playing} positionMs=${state.positionMs} forceSeek=$forceSeek"
            )
        }
    }

    private fun updateOpenXrUiState(reason: String) {
        if (!::bridge.isInitialized) return
        val playback = if (!smokeOnly && playerInitialized) {
            bridge.getFfmpegPlaybackState()
        } else {
            top.rootu.dddvr.xr.ui.OpenXrFfmpegPlaybackState()
        }
        val playing = playback.playing
        val buffering = playback.buffering
        val positionMs = playback.positionMs
        val durationMs = playback.durationMs
        val bufferedPositionMs = playback.bufferedPositionMs
        currentVideoIsHdr = playback.hdr || currentVideoIsHdr
        if (playback.width > 0 && playback.height > 0 &&
            (playback.width != lastNativeVideoWidth || playback.height != lastNativeVideoHeight)) {
            lastNativeVideoWidth = playback.width
            lastNativeVideoHeight = playback.height
            bridge.setVideoSize(playback.width, playback.height)
            Log.i(TAG, "XR_FFMPEG_VIDEO_SIZE width=${playback.width} height=${playback.height}")
        }
        val title = playbackRequest?.title?.takeIf { it.isNotBlank() } ?: "DDD-VR OpenXR Player"
        val audioTrackRows = if (!smokeOnly && playerInitialized) {
            bridge.getFfmpegAudioTracks()
        } else {
            emptyList()
        }
        val subtitleTrackRows = emptyList<top.rootu.dddvr.xr.ui.OpenXrTrackRow>()
        subtitlesEnabled = false
        val selectedAudioLabel = audioTrackRows.firstOrNull { it.selected }?.title.orEmpty()
        val currentVideoIs3d = playbackConfig.stereoMode != StereoInputMode.MONO
        val effectiveEnhanceVideo = enhanceVideo || currentVideoIsHdr || currentVideoIs3d
        val effectiveBrightness = when {
            currentVideoIsHdr -> 0.86f
            currentVideoIs3d -> 0.58f
            enhanceVideo -> 0.96f
            else -> 1.0f
        }
        val state = OpenXrPlayerUiState(
            visible = openXrUiVisible,
            pinned = activeModal != OpenXrModal.NONE,
            playing = playing,
            buffering = buffering,
            muted = muted,
            positionMs = positionMs,
            durationMs = durationMs,
            bufferedPositionMs = bufferedPositionMs,
            title = title,
            projectionModeLabel = projectionModeLabel(),
            audioTrackLabel = selectedAudioLabel,
            subtitleTrackLabel = "",
            activeModal = activeModal.nativeCode,
            activeSettingsTab = activeSettingsTab.nativeCode,
            hoverTarget = 0,
            display = OpenXrDisplayUiState(
                aspectRatio = aspectRatio,
                playbackSpeed = playbackSpeed,
                enhanceVideo = effectiveEnhanceVideo,
                hdrVideo = currentVideoIsHdr,
                brightness = effectiveBrightness
            ),
            subtitles = OpenXrSubtitlesUiState(
                enabled = subtitlesEnabled,
                delayMs = 0,
                sizeLabel = "Средний",
                positionLabel = "Ниже"
            ),
            audio = OpenXrAudioUiState(
                delayMs = 0,
                spatialAudio = spatialAudio
            ),
            playlistRows = buildPlaylistRows(),
            audioTracks = audioTrackRows,
            subtitleTracks = subtitleTrackRows
        )
        bridge.setPlayerUiState(state)
        if (reason != "tick") {
            Log.i(
                TAG,
                "XR_UI_STATE reason=$reason visible=$openXrUiVisible positionMs=$positionMs " +
                    "durationMs=$durationMs bufferedMs=$bufferedPositionMs playing=$playing " +
                    "buffering=$buffering modal=$activeModal tab=$activeSettingsTab " +
                    "enhanceVideo=$effectiveEnhanceVideo currentVideoIsHdr=$currentVideoIsHdr currentVideoIs3d=$currentVideoIs3d brightness=$effectiveBrightness " +
                    "audioRows=${audioTrackRows.size} nativeAudio=${playback.audioActive} " +
                    "subtitleTracks=${subtitleTrackRows.size} selectedAudio=$selectedAudioTrackIndex"
            )
        }
    }

    private fun updateAudioTrackOptions(reason: String) {
        if (smokeOnly || !playerInitialized || !::bridge.isInitialized) return
        val audioRows = bridge.getFfmpegAudioTracks()
        selectedAudioTrackIndex = audioRows.indexOfFirst { it.selected }.coerceAtLeast(0)
        val audioRowsLog = audioRows.joinToString {
            "${it.id}|title=${it.title}|sub=${it.subtitle}|selected=${it.selected}|enabled=${it.enabled}"
        }
        Log.i(TAG, "XR_TRACK_ROWS reason=$reason audio=$audioRowsLog")
        Log.i(TAG, "XR_AUDIO_TRACKS_UPDATE reason=$reason count=${audioRows.size} selected=$selectedAudioTrackIndex backend=ffmpeg")
        updateOpenXrUiState("audio_tracks_$reason")
    }

    private fun buildPlaylistRows(): List<OpenXrPlaylistRow> {
        val player = if (!smokeOnly && playerInitialized && ::playerManager.isInitialized) {
            playerManager.exoPlayer
        } else {
            null
        }
        if (player != null && player.mediaItemCount > 0) {
            return (0 until player.mediaItemCount).map { index ->
                val item = player.getMediaItemAt(index)
                val itemTitle = item.mediaMetadata.title?.toString()?.takeIf { it.isNotBlank() }
                    ?: playbackRequest?.title?.takeIf { it.isNotBlank() }
                    ?: "Видео ${index + 1}"
                OpenXrPlaylistRow(
                    id = "playlist:$index",
                    title = itemTitle,
                    subtitle = "${index + 1} / ${player.mediaItemCount}",
                    selected = index == player.currentMediaItemIndex
                )
            }
        }
        val fallbackTitle = playbackRequest?.title?.takeIf { it.isNotBlank() } ?: "Видео"
        return listOf(
            OpenXrPlaylistRow(
                id = "playlist:0",
                title = fallbackTitle,
                subtitle = "",
                selected = true
            )
        )
    }

    private fun projectionModeLabel(): String {
        val stereoLabel = when (playbackConfig.stereoMode) {
            StereoInputMode.SBS -> "SBS"
            StereoInputMode.SBS_REVERSED -> "SBS-R"
            StereoInputMode.OU -> "OU"
            StereoInputMode.OU_REVERSED -> "OU-R"
            StereoInputMode.VR_CAM_V1 -> "VRCAM1"
            StereoInputMode.VR_CAM_V2 -> "VRCAM2"
            else -> "2D"
        }
        return when (playbackConfig.screenMode) {
            OpenXrScreenMode.VR180 -> "180"
            OpenXrScreenMode.VR360 -> "360"
            OpenXrScreenMode.CURVED -> if (stereoLabel == "2D") "Curved" else stereoLabel
            OpenXrScreenMode.FLAT -> stereoLabel
        }
    }

    private fun applyAudioTrackSelection(index: Int): Boolean {
        if (smokeOnly || !playerInitialized || !::bridge.isInitialized) return false
        val rows = bridge.getFfmpegAudioTracks()
        if (index !in rows.indices) {
            Log.w(TAG, "XR_AUDIO_TRACK_SELECT_IGNORED invalid=$index count=${rows.size}")
            return false
        }
        val option = rows[index]
        val applied = bridge.selectFfmpegAudioTrack(option.id)
        if (!applied) {
            Log.w(TAG, "XR_AUDIO_TRACK_SELECT_FAILED index=$index id=${option.id}")
            return false
        }
        selectedAudioTrackIndex = index
        openXrUiVisible = true
        Log.i(TAG, "XR_AUDIO_TRACK_SELECTED index=$index id=${option.id} label=${option.title} backend=ffmpeg")
        updateAudioTrackOptions("audio_track_selected")
        return true
    }

    private fun saveLastPlaybackRequest(request: VrPlaybackRequest) {
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
            .edit()
            .putString(PREF_LAST_URI, request.uri.toString())
            .putString(PREF_LAST_TITLE, request.title)
            .apply()
    }

    private fun restoreLastPlaybackRequest(): VrPlaybackRequest? {
        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        val uri = prefs.getString(PREF_LAST_URI, null) ?: run {
            Log.w(TAG, "OPENXR_LAUNCH_NO_URI no last playback request")
            return null
        }
        val title = prefs.getString(PREF_LAST_TITLE, null)
        val restoredIntent = Intent(Intent.ACTION_VIEW, android.net.Uri.parse(uri)).apply {
            putExtra(Intent.EXTRA_TITLE, title)
        }
        val restored = VrIntentParser.parse(restoredIntent)
        Log.i(TAG, "OPENXR_LAUNCH_RESTORED_LAST_URI restored=${restored != null} uri=$uri")
        return restored
    }

    enum class XrStartState {
        NOT_REQUESTED,
        SCHEDULED,
        STARTING,
        STARTED,
        PAUSED_BEFORE_START,
        FAILED
    }

    private enum class OpenXrModal(val nativeCode: Int) {
        NONE(0),
        PLAYLIST(1),
        SETTINGS(2)
    }

    private enum class OpenXrSettingsTab(val nativeCode: Int) {
        DISPLAY(0),
        SUBTITLES(1),
        AUDIO(2);

        companion object {
            fun fromNative(value: Int): OpenXrSettingsTab {
                return values().firstOrNull { it.nativeCode == value } ?: DISPLAY
            }
        }
    }

    override fun onPlayPause() {
        if (!smokeOnly && playerInitialized) {
            val state = bridge.getFfmpegPlaybackState()
            bridge.setFfmpegPlaybackState(!state.playing, state.positionMs)
            openXrUiVisible = true
            updateOpenXrUiState("input_play_pause")
        }
    }
    override fun onSeekBy(deltaMs: Long) {
        if (!smokeOnly && playerInitialized) {
            val state = bridge.getFfmpegPlaybackState()
            val target = (state.positionMs + deltaMs).coerceIn(0L, state.durationMs.coerceAtLeast(0L))
            bridge.setFfmpegPlaybackState(state.playing, target, forceSeek = true)
            openXrUiVisible = true
            updateOpenXrUiState("input_seek")
        }
    }
    override fun onSeekToProgress(progressPermille: Int) {
        if (!smokeOnly && playerInitialized) {
            val state = bridge.getFfmpegPlaybackState()
            val duration = state.durationMs
            if (duration <= 0L) return
            val positionMs = (duration * progressPermille.coerceIn(0, 1000)) / 1000L
            openXrUiVisible = true
            bridge.setFfmpegPlaybackState(
                state.playing,
                positionMs,
                forceSeek = true
            )
            updateOpenXrUiState("input_timeline_seek")
            Log.i(TAG, "XR_TIMELINE_SEEK_APPLIED progress=$progressPermille positionMs=$positionMs durationMs=$duration")
        }
    }
    override fun onSelectAudioTrack(trackIndex: Int) { applyAudioTrackSelection(trackIndex) }
    override fun onPlayerUiAction(action: OpenXrPlayerUiAction) {
        when (action) {
            OpenXrPlayerUiAction.TogglePlaylist -> {
                activeModal = if (activeModal == OpenXrModal.PLAYLIST) OpenXrModal.NONE else OpenXrModal.PLAYLIST
                openXrUiVisible = true
            }
            OpenXrPlayerUiAction.ToggleSettings -> {
                activeModal = if (activeModal == OpenXrModal.SETTINGS) OpenXrModal.NONE else OpenXrModal.SETTINGS
                openXrUiVisible = true
            }
            OpenXrPlayerUiAction.ToggleVolume -> {
                muted = !muted
                if (!smokeOnly && playerInitialized) bridge.setFfmpegMuted(muted)
                openXrUiVisible = true
            }
            OpenXrPlayerUiAction.ToggleProjectionMenu -> {
                activeModal = OpenXrModal.SETTINGS
                activeSettingsTab = OpenXrSettingsTab.DISPLAY
                openXrUiVisible = true
                Log.i(TAG, "XR_PROJECTION_MENU_REQUEST current=${projectionModeLabel()}")
            }
            OpenXrPlayerUiAction.ToggleEnvironment -> {
                openXrUiVisible = true
                Log.i(TAG, "XR_ENVIRONMENT_TOGGLE_REQUEST")
            }
            is OpenXrPlayerUiAction.SetSettingsTab -> {
                activeModal = OpenXrModal.SETTINGS
                activeSettingsTab = OpenXrSettingsTab.fromNative(action.tab)
                openXrUiVisible = true
            }
            is OpenXrPlayerUiAction.SelectAudioTrack -> {
                if (!smokeOnly && playerInitialized) {
                    val applied = if (action.id.startsWith("legacy_audio:")) {
                        action.id.removePrefix("legacy_audio:").toIntOrNull()?.let(::applyAudioTrackSelection) ?: false
                    } else {
                        bridge.selectFfmpegAudioTrack(action.id).also {
                            if (it) updateAudioTrackOptions("ui_audio_select")
                        }
                    }
                    Log.i(TAG, "XR_AUDIO_TRACK_SELECT_REQUEST id=${action.id} applied=$applied")
                }
                openXrUiVisible = true
            }
            is OpenXrPlayerUiAction.SelectSubtitleTrack -> {
                Log.i(TAG, "XR_SUBTITLE_TRACK_SELECT_REQUEST id=${action.id} applied=false backend=ffmpeg_pending")
                openXrUiVisible = true
            }
            is OpenXrPlayerUiAction.SelectPlaylistItem -> {
                selectPlaylistItem(action.id)
                activeModal = OpenXrModal.NONE
                openXrUiVisible = true
            }
            is OpenXrPlayerUiAction.SetAspectRatio -> {
                aspectRatio = action.value.takeIf { it.isNotBlank() } ?: "Оригинал"
                openXrUiVisible = true
            }
            is OpenXrPlayerUiAction.SetPlaybackSpeed -> {
                playbackSpeed = 1.0f
                Log.w(TAG, "XR_PLAYBACK_SPEED_IGNORED requested=${action.value} backend=ffmpeg")
                openXrUiVisible = true
            }
            OpenXrPlayerUiAction.ToggleEnhanceVideo -> {
                enhanceVideo = !enhanceVideo
                openXrUiVisible = true
            }
            OpenXrPlayerUiAction.ToggleSpatialAudio -> {
                spatialAudio = !spatialAudio
                openXrUiVisible = true
            }
            OpenXrPlayerUiAction.ToggleSubtitles -> {
                setSubtitlesEnabled(!subtitlesEnabled)
                openXrUiVisible = true
            }
            is OpenXrPlayerUiAction.Unknown -> {
                Log.w(TAG, "XR_PLAYER_UI_ACTION_UNKNOWN type=${action.actionType}")
            }
        }
        updateOpenXrUiState("player_ui_action_${action.javaClass.simpleName}")
    }

    private fun setSubtitlesEnabled(enabled: Boolean) {
        subtitlesEnabled = false
        Log.i(TAG, "XR_SUBTITLES_TOGGLE enabled=$enabled applied=false backend=ffmpeg_pending")
    }

    private fun selectPlaylistItem(id: String) {
        val index = id.removePrefix("playlist:").toIntOrNull() ?: return
        if (index != 0 || smokeOnly || !playerInitialized) return
        val state = bridge.getFfmpegPlaybackState()
        bridge.setFfmpegPlaybackState(true, 0L, forceSeek = true)
        Log.i(TAG, "XR_PLAYLIST_ITEM_SELECTED index=$index")
    }
    override fun onRecenter() { OpenXrDebugOverlay.logSessionState("recenter_request") }
    override fun onShowMenu() {
        openXrUiVisible = !openXrUiVisible
        updateOpenXrUiState("show_menu_request")
        OpenXrDebugOverlay.logSessionState("show_menu_request visible=$openXrUiVisible")
    }
    override fun onExit() { finish() }

    companion object {
        private const val TAG = "DDDVR/OpenXR"
        private const val FIRST_XR_START_DELAY_MS = 0L
        private const val XR_RETRY_DELAY_MS = 500L
        private const val PLAYER_START_DELAY_MS = 200L
        private const val UI_STATE_UPDATE_MS = 250L
        private const val FFMPEG_VIDEO_START_RETRY_DELAY_MS = 12_000L
        private const val MAX_FFMPEG_VIDEO_START_ATTEMPTS = 3
        private const val MAX_NOT_RESUMED_RETRIES = 5
        private const val MAX_SOURCE_ERROR_RETRIES = 20
        private const val SOURCE_ERROR_RETRY_DELAY_MS = 500L
        private const val PREFS_NAME = "openxr_player"
        private const val PREF_LAST_URI = "last_uri"
        private const val PREF_LAST_TITLE = "last_title"
    }
}
