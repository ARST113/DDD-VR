package top.rootu.dddvr.vr.player

import top.rootu.dddvr.vr.projection.ProjectionType
import top.rootu.dddvr.vr.stereo.StereoInputMode

data class VrPlaybackState(
    val isPlaying: Boolean,
    val positionMs: Long,
    val durationMs: Long,
    val bufferedPositionMs: Long,
    val title: String?,
    val currentIndex: Int,
    val itemCount: Int,
    val stereoMode: StereoInputMode,
    val projectionType: ProjectionType,
    val swapEyes: Boolean,
    val hasError: Boolean,
    val errorMessage: String?
)
