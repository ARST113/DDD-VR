package top.rootu.dddvr.xr.model

import top.rootu.dddvr.vr.activity.VrPlaybackRequest
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
            val mode = when (request.vrConfig.projectionMode.name) {
                "VR_CURVED_SCREEN" -> OpenXrScreenMode.CURVED
                else -> OpenXrScreenMode.FLAT
            }
            return OpenXrPlaybackConfig(
                stereoMode = request.stereoInputMode,
                swapEyes = request.vrConfig.swapEyes,
                screenMode = mode,
                startPositionMs = request.startPositionMs
            )
        }
    }
}
