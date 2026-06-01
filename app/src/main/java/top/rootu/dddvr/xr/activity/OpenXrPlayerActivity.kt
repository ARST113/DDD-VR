package top.rootu.dddvr.xr.activity

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.KeyEvent
import android.view.Surface
import android.view.WindowManager
import androidx.media3.common.C
import androidx.media3.common.Format
import androidx.media3.common.Player
import androidx.media3.common.PlaybackException
import androidx.media3.common.TrackSelectionOverride
import androidx.media3.common.Tracks
import top.rootu.dddvr.core.playback.PlaybackSession
import top.rootu.dddvr.logic.TrackLogic
import top.rootu.dddvr.model.MediaItem
import top.rootu.dddvr.player.PlayerManager
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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "ACTIVITY_ON_CREATE_BEGIN")
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val parsedRequest = VrIntentParser.parse(intent)
        val request = parsedRequest ?: restoreLastPlaybackRequest()
        playbackRequest = request

        smokeOnly = intent?.getBooleanExtra("openxr_smoke_only", false) == true || request == null
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
                    playbackSession.play()
                    openXrUiVisible = true
                    updateOpenXrUiState("key_play")
                }
                true
            }
            VrKeyAction.PAUSE -> {
                if (!smokeOnly && playerInitialized) {
                    playbackSession.pause()
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
            if (!smokeOnly && playerInitialized) {
                runCatching { playbackSession.clearSurface(surface) }
                    .onFailure { Log.e(TAG, "Unable to clear playback surface", it) }
            }
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
        Log.i(TAG, "XR_VIDEO_SURFACE_READY surface=$surface playerInitialized=$playerInitialized")
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
        playerManager = PlayerManager(this, openXrPlayerListener)
        playerManager.onPlayerCreated = {
            Log.i(TAG, "XR_PLAYER_CREATED hasSurface=${activeSurface != null}")
            it.volume = 1f
            activeSurface?.let { surface -> playbackSession.attachSurface(surface) }
            updateOpenXrUiState("player_created")
        }
        playerManager.onVideoFormatChanged = { format -> onVideoFormatChanged(format) }
        playerManager.onAudioOutputFormatChanged = { info -> Log.i(TAG, "XR_AUDIO_FORMAT $info") }
        playerManager.onMetadataAvailable = { updateAudioTrackOptions("metadata_available") }
        playbackSession = PlaybackSession(playerManager)
        playerInitialized = true
        playerManager.loadPlaylist(
            listOf(MediaItem(uri = request.uri, title = request.title, startPositionMs = request.startPositionMs)),
            0,
            request.startPositionMs
        )
        activeSurface?.let { playbackSession.attachSurface(it) }
        updateAudioTrackOptions("player_start")
        updateOpenXrUiState("player_start_end")
        Log.i(TAG, "PLAYER_START_END")
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
                activeSurface?.let { playbackSession.attachSurface(it) }
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
        if (playbackSession.isPlaying || playbackSession.wantsToPlay) {
            resumePlaybackAfterFocusLoss = true
            playbackSession.pause()
            updateOpenXrUiState("focus_loss_pause")
            Log.i(TAG, "PLAYER_PAUSED_FOR_XR_FOCUS_LOSS reason=$reason position=${playbackSession.currentPositionMs}")
        } else {
            resumePlaybackAfterFocusLoss = false
            Log.i(TAG, "PLAYER_PAUSE_FOR_XR_FOCUS_LOSS_SKIPPED notPlaying reason=$reason")
        }
    }

    private fun isForegroundForPlayback(): Boolean {
        return resumed && (topResumed || hasWindowFocus)
    }

    private fun resumePlaybackIfForeground(reason: String) {
        if (smokeOnly || !playerInitialized || !resumePlaybackAfterFocusLoss) return
        if (!isForegroundForPlayback()) {
            Log.i(TAG, "PLAYER_RESUME_AFTER_XR_FOCUS_LOSS_DEFERRED reason=$reason resumed=$resumed topResumed=$topResumed hasWindowFocus=$hasWindowFocus")
            return
        }
        resumePlaybackAfterFocusLoss = false
        playbackSession.play()
        updateOpenXrUiState("focus_return_play")
        Log.i(TAG, "PLAYER_RESUMED_AFTER_XR_FOCUS_LOSS reason=$reason position=${playbackSession.currentPositionMs}")
    }

    private fun onVideoFormatChanged(format: Format) {
        val width = format.width
        val height = format.height
        if (width <= 0 || height <= 0) return
        Log.i(TAG, "XR_VIDEO_FORMAT width=$width height=$height sampleMime=${format.sampleMimeType}")
        bridge.setVideoSize(width, height)
    }

    private fun updateOpenXrUiState(reason: String) {
        if (!::bridge.isInitialized) return
        val playing = !smokeOnly && playerInitialized && playbackSession.isPlaying
        val buffering = !smokeOnly &&
            playerInitialized &&
            playerManager.exoPlayer?.playbackState == Player.STATE_BUFFERING
        val positionMs = if (!smokeOnly && playerInitialized) playbackSession.currentPositionMs else 0L
        val durationMs = if (!smokeOnly && playerInitialized) playbackSession.durationMs.coerceAtLeast(0L) else 0L
        val bufferedPositionMs = if (!smokeOnly && playerInitialized) playbackSession.bufferedPositionMs.coerceAtLeast(0L) else 0L
        val title = playbackRequest?.title?.takeIf { it.isNotBlank() } ?: "DDD-VR OpenXR Player"
        val audioLabels = audioOptions.map { TrackLogic.buildTrackLabel(it, this) }.toTypedArray()
        val selectedAudioLabel = audioLabels.getOrNull(selectedAudioTrackIndex).orEmpty()
        bridge.setUiState(
            visible = openXrUiVisible,
            playing = playing,
            buffering = buffering,
            positionMs = positionMs,
            durationMs = durationMs,
            bufferedPositionMs = bufferedPositionMs,
            title = title,
            stereoModeLabel = stereoModeLabel(),
            audioTrackLabel = selectedAudioLabel,
            audioTrackLabels = audioLabels,
            selectedAudioTrackIndex = selectedAudioTrackIndex
        )
        if (reason != "tick") {
            Log.i(
                TAG,
                "XR_UI_STATE reason=$reason visible=$openXrUiVisible positionMs=$positionMs " +
                    "durationMs=$durationMs bufferedMs=$bufferedPositionMs playing=$playing " +
                    "buffering=$buffering audioTracks=${audioLabels.size} selectedAudio=$selectedAudioTrackIndex"
            )
        }
    }

    private fun updateAudioTrackOptions(reason: String) {
        if (smokeOnly || !playerInitialized || !::playerManager.isInitialized) return
        val player = playerManager.exoPlayer ?: return
        val (tracks, selectedIndex) = TrackLogic.extractAudioTracks(
            player.currentTracks,
            playerManager.getTrackMetadata()
        )
        audioOptions = tracks
        selectedAudioTrackIndex = selectedIndex.coerceIn(0, (tracks.size - 1).coerceAtLeast(0))
        Log.i(TAG, "XR_AUDIO_TRACKS_UPDATE reason=$reason count=${audioOptions.size} selected=$selectedAudioTrackIndex")
        updateOpenXrUiState("audio_tracks_$reason")
    }

    private fun stereoModeLabel(): String {
        return when (playbackConfig.stereoMode) {
            StereoInputMode.SBS -> "SBS"
            StereoInputMode.SBS_REVERSED -> "SBS-R"
            StereoInputMode.OU -> "OU"
            StereoInputMode.OU_REVERSED -> "OU-R"
            else -> "2D"
        }
    }

    private fun applyAudioTrackSelection(index: Int) {
        if (smokeOnly || !playerInitialized || !::playerManager.isInitialized) return
        if (index !in audioOptions.indices) {
            Log.w(TAG, "XR_AUDIO_TRACK_SELECT_IGNORED invalid=$index count=${audioOptions.size}")
            return
        }
        val option = audioOptions[index]
        val player = playerManager.exoPlayer ?: return
        val builder = player.trackSelectionParameters.buildUpon()
        if (option.isOff) {
            builder.setTrackTypeDisabled(C.TRACK_TYPE_AUDIO, true)
        } else {
            builder.setTrackTypeDisabled(C.TRACK_TYPE_AUDIO, false)
            option.group?.let { group ->
                builder.setOverrideForType(
                    TrackSelectionOverride(
                        group.mediaTrackGroup,
                        option.trackIndex
                    )
                )
            }
        }
        player.trackSelectionParameters = builder.build()
        selectedAudioTrackIndex = index
        openXrUiVisible = true
        Log.i(TAG, "XR_AUDIO_TRACK_SELECTED index=$index label=${TrackLogic.buildTrackLabel(option, this)}")
        updateOpenXrUiState("audio_track_selected")
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

    override fun onPlayPause() {
        if (!smokeOnly && playerInitialized) {
            if (playbackSession.isPlaying) playbackSession.pause() else playbackSession.play()
            openXrUiVisible = true
            updateOpenXrUiState("input_play_pause")
        }
    }
    override fun onSeekBy(deltaMs: Long) {
        if (!smokeOnly && playerInitialized) {
            playbackSession.seekTo(playbackSession.currentPositionMs + deltaMs)
            openXrUiVisible = true
            updateOpenXrUiState("input_seek")
        }
    }
    override fun onSeekToProgress(progressPermille: Int) {
        if (!smokeOnly && playerInitialized) {
            val duration = playbackSession.durationMs
            if (duration <= 0L) return
            val positionMs = (duration * progressPermille.coerceIn(0, 1000)) / 1000L
            playbackSession.seekTo(positionMs)
            openXrUiVisible = true
            updateOpenXrUiState("input_timeline_seek")
            Log.i(TAG, "XR_TIMELINE_SEEK_APPLIED progress=$progressPermille positionMs=$positionMs durationMs=$duration")
        }
    }
    override fun onSelectAudioTrack(trackIndex: Int) { applyAudioTrackSelection(trackIndex) }
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
        private const val MAX_NOT_RESUMED_RETRIES = 5
        private const val MAX_SOURCE_ERROR_RETRIES = 20
        private const val SOURCE_ERROR_RETRY_DELAY_MS = 500L
        private const val PREFS_NAME = "openxr_player"
        private const val PREF_LAST_URI = "last_uri"
        private const val PREF_LAST_TITLE = "last_title"
    }
}
