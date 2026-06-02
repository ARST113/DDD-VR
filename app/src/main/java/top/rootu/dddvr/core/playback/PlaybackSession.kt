package top.rootu.dddvr.core.playback

import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Surface
import androidx.media3.common.Player
import top.rootu.dddvr.player.PlayerManager

class PlaybackSession(
    private val playerManager: PlayerManager
) {
    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile
    private var lastKnownIsPlaying: Boolean = false

    @Volatile
    private var lastKnownWantsToPlay: Boolean = false

    @Volatile
    private var lastKnownPositionMs: Long = 0L

    @Volatile
    private var currentVideoSurface: Surface? = null

    @Volatile
    var hasAttachedVideoSurface: Boolean = false
        private set

    val isPlaying: Boolean
        get() {
            if (!isMainThread()) {
                return lastKnownIsPlaying
            }

            val value = playerManager.exoPlayer?.isPlaying == true
            lastKnownIsPlaying = value
            return value
        }

    val currentPositionMs: Long
        get() {
            if (!isMainThread()) {
                return lastKnownPositionMs
            }

            val value = playerManager.exoPlayer?.currentPosition ?: 0L
            lastKnownPositionMs = value
            return value
        }


    val durationMs: Long
        get() {
            if (!isMainThread()) return 0L
            return playerManager.exoPlayer?.duration ?: 0L
        }

    val bufferedPositionMs: Long
        get() {
            if (!isMainThread()) return 0L
            return playerManager.exoPlayer?.bufferedPosition ?: 0L
        }

    val playbackSpeed: Float
        get() {
            if (!isMainThread()) return 1f
            return playerManager.exoPlayer?.playbackParameters?.speed ?: 1f
        }

    val isBuffering: Boolean
        get() {
            if (!isMainThread()) return false
            return playerManager.exoPlayer?.playbackState == Player.STATE_BUFFERING
        }

    val wantsToPlay: Boolean
        get() {
            if (!isMainThread()) {
                return lastKnownWantsToPlay
            }

            val value = playerManager.exoPlayer?.playWhenReady == true
            lastKnownWantsToPlay = value
            return value
        }

    fun attachSurface(surface: Surface) {
        currentVideoSurface = surface
        runOnPlayerThread {
            val player = playerManager.exoPlayer
            if (player == null) {
                hasAttachedVideoSurface = false
                Log.i(TAG, "VR_SURFACE_ATTACH_DEFERRED surface=$surface")
                return@runOnPlayerThread
            }

            player.setVideoSurface(surface)
            hasAttachedVideoSurface = true
            Log.i(TAG, "VR_SURFACE_ATTACHED surface=$surface")
        }
    }

    fun attachCurrentSurface() {
        val surface = currentVideoSurface
        if (surface == null) {
            Log.i(TAG, "VR_SURFACE_ATTACH_SKIPPED no current surface")
            return
        }
        attachSurface(surface)
    }

    fun clearSurface(surface: Surface? = null) {
        runOnPlayerThread {
            val player = playerManager.exoPlayer

            if (surface != null) {
                player?.clearVideoSurface(surface)
            } else {
                player?.clearVideoSurface()
            }

            if (surface == null || surface == currentVideoSurface) {
                currentVideoSurface = null
                hasAttachedVideoSurface = false
            }
        }
    }

    fun prepare() {
        runOnPlayerThread {
            playerManager.exoPlayer?.prepare()
        }
    }

    fun play() {
        runOnPlayerThread {
            val player = playerManager.exoPlayer ?: return@runOnPlayerThread
            player.playWhenReady = true
            player.play()
            lastKnownWantsToPlay = true
            lastKnownIsPlaying = true
        }
    }

    fun pause() {
        runOnPlayerThread {
            val player = playerManager.exoPlayer
            player?.playWhenReady = false
            player?.pause()
            lastKnownWantsToPlay = false
            lastKnownIsPlaying = false
            lastKnownPositionMs = player?.currentPosition ?: lastKnownPositionMs
        }
    }

    fun seekTo(positionMs: Long) {
        runOnPlayerThread {
            val target = positionMs.coerceAtLeast(0L)
            playerManager.exoPlayer?.seekTo(target)
            lastKnownPositionMs = target
        }
    }

    fun seekBy(deltaMs: Long) {
        seekTo(currentPositionMs + deltaMs)
    }

    fun setMuted(muted: Boolean) {
        runOnPlayerThread {
            playerManager.exoPlayer?.volume = if (muted) 0f else 1f
        }
    }

    fun setPlaybackSpeed(speed: Float) {
        runOnPlayerThread {
            playerManager.exoPlayer?.setPlaybackSpeed(speed.coerceIn(0.25f, 3.0f))
        }
    }

    fun release() {
        runOnPlayerThread {
            lastKnownIsPlaying = false
            lastKnownWantsToPlay = false
            lastKnownPositionMs = 0L
            currentVideoSurface = null
            hasAttachedVideoSurface = false
            playerManager.releasePlayer(isFinalRelease = true)
        }
    }

    private fun runOnPlayerThread(action: () -> Unit) {
        if (isMainThread()) {
            action()
        } else {
            mainHandler.post(action)
        }
    }

    private fun isMainThread(): Boolean {
        return Looper.myLooper() == Looper.getMainLooper()
    }

    private companion object {
        const val TAG = "DDDVR/PlaybackSession"
    }
}
