package top.rootu.dddvr.xr.model

import top.rootu.dddvr.vr.activity.VrPlaybackRequest
import top.rootu.dddvr.vr.model.ProjectionMode
import top.rootu.dddvr.vr.model.StereoLayout
import top.rootu.dddvr.vr.stereo.StereoInputMode

data class OpenXrPlaybackConfig(
    val stereoMode: StereoInputMode,
    val swapEyes: Boolean,
    val screenMode: OpenXrScreenMode,
    val startPositionMs: Long,
    val screenDistanceMeters: Float = 3.5f,
    val screenWidthMeters: Float = 4.5f,
    val screenCurveRadians: Float = 0.45f
) {
    companion object {
        fun from(request: VrPlaybackRequest): OpenXrPlaybackConfig {
            val effectiveStereo = when {
                request.hasStereoLayoutExtra && request.vrConfig.stereoLayout == StereoLayout.MONO -> StereoInputMode.MONO
                request.hasStereoLayoutExtra && request.vrConfig.stereoLayout == StereoLayout.SBS -> StereoInputMode.SBS
                request.hasStereoLayoutExtra && request.vrConfig.stereoLayout == StereoLayout.OU -> StereoInputMode.OU
                else -> request.stereoInputMode
            }
            val mode = when (request.vrConfig.projectionMode) {
                ProjectionMode.VR_CURVED_SCREEN -> OpenXrScreenMode.CURVED
                ProjectionMode.VR_FLAT_SCREEN -> OpenXrScreenMode.FLAT
                ProjectionMode.VR180 -> OpenXrScreenMode.VR180
                ProjectionMode.VR360 -> OpenXrScreenMode.VR360
            }
            return OpenXrPlaybackConfig(
                stereoMode = effectiveStereo,
                swapEyes = request.vrConfig.swapEyes,
                screenMode = mode,
                startPositionMs = request.startPositionMs,
                screenCurveRadians = if (mode == OpenXrScreenMode.CURVED) 0.65f else 0.45f
            )
        }
    }
}
