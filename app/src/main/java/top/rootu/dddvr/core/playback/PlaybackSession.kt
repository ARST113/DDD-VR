package top.rootu.dddvr.core.playback

import android.view.Surface
import top.rootu.dddplayer.player.PlayerManager

class PlaybackSession(
    private val playerManager: PlayerManager
) {
    val isPlaying: Boolean
        get() = playerManager.exoPlayer?.isPlaying == true

    val currentPositionMs: Long
        get() = playerManager.exoPlayer?.currentPosition ?: 0L

    fun attachSurface(surface: Surface) {
        playerManager.exoPlayer?.setVideoSurface(surface)
    }

    fun prepare() {
        playerManager.exoPlayer?.prepare()
    }

    fun play() {
        playerManager.exoPlayer?.playWhenReady = true
    }

    fun pause() {
        playerManager.exoPlayer?.playWhenReady = false
    }

    fun seekTo(positionMs: Long) {
        playerManager.exoPlayer?.seekTo(positionMs.coerceAtLeast(0L))
    }

    fun setMuted(muted: Boolean) {
        playerManager.exoPlayer?.volume = if (muted) 0f else 1f
    }

    fun release() = playerManager.release()
}
