package top.rootu.dddvr.xr.activity

import android.content.pm.ActivityInfo
import android.os.Bundle
import android.util.Log
import android.view.Surface
import android.view.WindowManager
import androidx.appcompat.app.AppCompatActivity
import androidx.media3.common.Player
import top.rootu.dddvr.core.playback.PlaybackSession
import top.rootu.dddvr.model.MediaItem
import top.rootu.dddvr.player.PlayerManager
import top.rootu.dddvr.vr.activity.VrIntentParser
import top.rootu.dddvr.xr.bridge.OpenXrBridge
import top.rootu.dddvr.xr.model.OpenXrPlaybackConfig
import top.rootu.dddvr.xr.ui.OpenXrDebugOverlay

class OpenXrPlayerActivity : AppCompatActivity(), OpenXrBridge.Callbacks {
    private lateinit var playerManager: PlayerManager
    private lateinit var playbackSession: PlaybackSession
    private lateinit var bridge: OpenXrBridge
    private var activeSurface: Surface? = null
    private var initialized = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val request = VrIntentParser.parse(intent) ?: run {
            Log.e("DDDVR/OpenXR", "Invalid intent for OpenXrPlayerActivity")
            finish()
            return
        }

        playerManager = PlayerManager(this, object : Player.Listener {})
        playbackSession = PlaybackSession(playerManager)

        playerManager.loadPlaylist(
            listOf(MediaItem(uri = request.uri, title = request.title, startPositionMs = request.startPositionMs)),
            0,
            request.startPositionMs
        )

        val config = OpenXrPlaybackConfig.from(request)
        OpenXrDebugOverlay.logStartup(config)

        bridge = OpenXrBridge(this, this, config)
        val startOk = runCatching { bridge.start() }
            .onFailure { Log.e("DDDVR/OpenXR", "Unable to start OpenXR bridge", it) }
            .getOrDefault(false)
        if (!startOk) {
            Log.e("DDDVR/OpenXR", "OpenXR bridge start failed (null/invalid native handle)")
            finish()
            return
        }
        initialized = true
    }

    override fun onResume() {
        super.onResume()
        if (initialized) runCatching { bridge.onResume() }
    }

    override fun onPause() {
        if (initialized) runCatching { bridge.onPause() }
        super.onPause()
    }

    override fun onDestroy() {
        if (initialized) {
            activeSurface?.let {
                playbackSession.clearSurface(it)
                it.release()
            }
            runCatching { bridge.destroy() }
            playbackSession.release()
        }
        super.onDestroy()
    }

    override fun onVideoSurfaceReady(surface: Surface) {
        activeSurface?.let { playbackSession.clearSurface(it) }
        activeSurface = surface
        playbackSession.attachSurface(surface)
        OpenXrDebugOverlay.logSurfaceAttached(isAttached = true)
    }

    override fun onPlayPause() { if (playbackSession.isPlaying) playbackSession.pause() else playbackSession.play() }
    override fun onSeekBy(deltaMs: Long) { playbackSession.seekTo(playbackSession.currentPositionMs + deltaMs) }
    override fun onRecenter() { OpenXrDebugOverlay.logSessionState("recenter_request") }
    override fun onExit() { finish() }
}
