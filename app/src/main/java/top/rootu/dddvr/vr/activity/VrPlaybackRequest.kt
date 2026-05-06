package top.rootu.dddvr.vr.activity

import android.net.Uri
import top.rootu.dddvr.vr.model.VrPlaybackConfig
import top.rootu.dddvr.vr.projection.ProjectionType
import top.rootu.dddvr.vr.stereo.StereoInputMode

data class VrPlaybackRequest(
    val uri: Uri,
    val title: String?,
    val stereoInputMode: StereoInputMode,
    val projectionType: ProjectionType,
    val startPositionMs: Long,
    val vrConfig: VrPlaybackConfig
)
