package top.rootu.dddvr.vr.model

enum class ProjectionMode {
    VR180,
    VR360,
    VR_CURVED_SCREEN,
    VR_FLAT_SCREEN
}

enum class StereoLayout {
    MONO,
    SBS,
    OU
}

enum class StereoPacking {
    FULL,
    HALF
}

data class VrPlaybackConfig(
    val projectionMode: ProjectionMode,
    val stereoLayout: StereoLayout,
    val stereoPacking: StereoPacking,
    val swapEyes: Boolean = false
)
