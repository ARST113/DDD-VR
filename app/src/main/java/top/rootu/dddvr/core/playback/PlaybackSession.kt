package top.rootu.dddvr.core.playback

import android.os.Handler
import android.os.Looper
import android.view.Surface
import top.rootu.dddvr.player.PlayerManager

class PlaybackSession(
    private val playerManager: PlayerManager
) {
    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile
    private var lastKnownIsPlaying: Boolean = false

    @Volatile
    private var lastKnownPositionMs: Long = 0L

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

    fun attachSurface(surface: Surface) {
        runOnPlayerThread {
            playerManager.exoPlayer?.setVideoSurface(surface)
        }
    }

    fun clearSurface(surface: Surface? = null) {
        runOnPlayerThread {
            val player = playerManager.exoPlayer ?: return@runOnPlayerThread

            if (surface != null) {
                player.clearVideoSurface(surface)
            } else {
                player.clearVideoSurface()
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
            playerManager.exoPlayer?.playWhenReady = true
            lastKnownIsPlaying = true
        }
    }

    fun pause() {
        runOnPlayerThread {
            playerManager.exoPlayer?.playWhenReady = false
            lastKnownIsPlaying = false
            lastKnownPositionMs = playerManager.exoPlayer?.currentPosition ?: lastKnownPositionMs
        }
    }

    fun seekTo(positionMs: Long) {
        runOnPlayerThread {
            val target = positionMs.coerceAtLeast(0L)
            playerManager.exoPlayer?.seekTo(target)
            lastKnownPositionMs = target
        }
    }

    fun setMuted(muted: Boolean) {
        runOnPlayerThread {
            playerManager.exoPlayer?.volume = if (muted) 0f else 1f
        }
    }

    fun release() {
        runOnPlayerThread {
            lastKnownIsPlaying = false
            lastKnownPositionMs = 0L
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
}
