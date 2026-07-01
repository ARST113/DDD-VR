package top.rootu.dddvr.vr.player

import androidx.media3.common.C
import top.rootu.dddvr.core.playback.PlaybackSession
import top.rootu.dddvr.vr.activity.VrPlayerActivity
import top.rootu.dddvr.vr.projection.ProjectionManager
import top.rootu.dddvr.vr.projection.ProjectionType
import top.rootu.dddvr.vr.stereo.StereoInputMode
import top.rootu.dddvr.vr.stereo.StereoUvMapper

class VrPlayerController(
    private val activity: VrPlayerActivity,
    private val playbackSession: PlaybackSession,
    private val projectionManagerProvider: () -> ProjectionManager,
    private val stereoUvMapper: StereoUvMapper,
    private val recenterHeadPose: () -> Unit = {},
    private val playbackErrorProvider: () -> String? = { null }
) {
    fun play() = playbackSession.play()
    fun pause() = playbackSession.pause()
    fun togglePlay() = if (isPlaying()) pause() else play()
    fun seekBy(deltaMs: Long) = seekTo(getCurrentPositionMs() + deltaMs)
    fun seekTo(positionMs: Long) = playbackSession.seekTo(positionMs.coerceAtLeast(0L))
    fun previousItem() = playbackSession.seekTo(0)
    fun nextItem() = Unit
    fun setStereoMode(mode: StereoInputMode) { stereoUvMapper.stereoInputMode = mode }
    fun getStereoMode(): StereoInputMode = stereoUvMapper.stereoInputMode
    fun setProjection(type: ProjectionType) = runCatching {
        projectionManagerProvider().setCurrentProjectionType(type)
    }.getOrDefault(Unit)

    fun getProjection(): ProjectionType = runCatching {
        projectionManagerProvider().currentProjectionType
    }.getOrDefault(ProjectionType.FLAT)
    fun swapEyes(enabled: Boolean) { stereoUvMapper.swapEyes = enabled }
    fun toggleSwapEyes() { stereoUvMapper.swapEyes = !stereoUvMapper.swapEyes }
    fun recenter() = recenterHeadPose()

    fun toggleStereoMode() {
        val next = when (stereoUvMapper.stereoInputMode) {
            StereoInputMode.MONO -> StereoInputMode.SBS
            StereoInputMode.SBS -> StereoInputMode.SBS_REVERSED
            StereoInputMode.SBS_REVERSED -> StereoInputMode.OU
            StereoInputMode.OU -> StereoInputMode.OU_REVERSED
            StereoInputMode.OU_REVERSED -> StereoInputMode.VR_CAM_V1
            StereoInputMode.VR_CAM_V1 -> StereoInputMode.VR_CAM_V2
            StereoInputMode.VR_CAM_V2 -> StereoInputMode.MONO
            else -> StereoInputMode.MONO
        }
        setStereoMode(next)
    }
    fun exitVrMode() { pause(); activity.finish() }
    fun getCurrentPositionMs(): Long = playbackSession.currentPositionMs
    fun getDurationMs(): Long = playbackSession.durationMs
    fun isPlaying(): Boolean = playbackSession.isPlaying
    fun getCurrentTitle(): String? = null
    fun getState(): VrPlaybackState {
        val errorMessage = playbackErrorProvider()
        return VrPlaybackState(
            isPlaying = isPlaying(),
            positionMs = getCurrentPositionMs(),
            durationMs = getDurationMs().takeIf { it != C.TIME_UNSET } ?: 0L,
            bufferedPositionMs = playbackSession.bufferedPositionMs,
            title = getCurrentTitle(),
            currentIndex = 0,
            itemCount = 1,
            stereoMode = getStereoMode(),
            projectionType = getProjection(),
            swapEyes = stereoUvMapper.swapEyes,
            hasError = errorMessage != null,
            errorMessage = errorMessage
        )
    }

    fun release() = playbackSession.release()
}
