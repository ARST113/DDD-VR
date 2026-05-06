package top.rootu.dddvr.xr.model

import top.rootu.dddvr.vr.activity.VrPlaybackRequest
import top.rootu.dddvr.vr.model.StereoLayout
import top.rootu.dddvr.vr.stereo.StereoInputMode

data class OpenXrPlaybackConfig(
    val stereoMode: StereoInputMode,
    val swapEyes: Boolean,
    val screenMode: OpenXrScreenMode,
    val startPositionMs: Long,
    val screenDistanceMeters: Float = 3.5f,
    val screenWidthMeters: Float = 4.5f
) {
    companion object {
        fun from(request: VrPlaybackRequest): OpenXrPlaybackConfig {
            val effectiveStereo = when {
                request.hasStereoLayoutExtra && request.vrConfig.stereoLayout == StereoLayout.MONO -> StereoInputMode.MONO
                request.vrConfig.stereoLayout == StereoLayout.SBS -> StereoInputMode.SBS
                request.vrConfig.stereoLayout == StereoLayout.OU -> StereoInputMode.OU
                else -> request.stereoInputMode
            }
            val mode = when (request.vrConfig.projectionMode.name) {
                "VR_CURVED_SCREEN" -> OpenXrScreenMode.CURVED
                else -> OpenXrScreenMode.FLAT
            }
            return OpenXrPlaybackConfig(
                stereoMode = effectiveStereo,
                swapEyes = request.vrConfig.swapEyes,
                screenMode = mode,
                startPositionMs = request.startPositionMs
            )
        }
    }
}
