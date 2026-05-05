package top.rootu.dddvr.vr.player

import top.rootu.dddvr.core.playback.PlaybackSession
import top.rootu.dddvr.vr.activity.VrPlayerActivity
import top.rootu.dddvr.vr.input.VrInputController
import top.rootu.dddvr.vr.projection.ProjectionManager
import top.rootu.dddvr.vr.projection.ProjectionType
import top.rootu.dddvr.vr.ui.VrUiLayer

class VrPlayerController(
    private val activity: VrPlayerActivity,
    private val playbackSession: PlaybackSession,
    private val projectionManagerProvider: () -> ProjectionManager,
    private val uiLayer: VrUiLayer,
    private val inputController: VrInputController
) {
    fun enterVrMode() {
        uiLayer.show()
        inputController.enable()
    }

    fun exitVrMode() {
        playbackSession.pause()
        activity.finish()
    }

    fun play() = playbackSession.play()
    fun pause() = playbackSession.pause()
    fun togglePlay() = if (playbackSession.isPlaying) pause() else play()
    fun seekBy(deltaMs: Long) = playbackSession.seekTo(playbackSession.currentPositionMs + deltaMs)
    fun setProjection(type: ProjectionType) = projectionManagerProvider().setCurrentProjectionType(type)
    fun setMuted(muted: Boolean) = playbackSession.setMuted(muted)
    fun release() = playbackSession.release()
}
