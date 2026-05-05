package top.rootu.dddvr.vr.stereo

import top.rootu.dddvr.vr.camera.Eye

data class UvTransform(
    val uOffset: Float = 0f,
    val vOffset: Float = 0f,
    val uScale: Float = 1f,
    val vScale: Float = 1f
)

class StereoUvMapper {
    var stereoInputMode: StereoInputMode = StereoInputMode.MONO
    var swapEyes: Boolean = false

    fun getUvTransform(eye: Eye): UvTransform {
        val actualEye = if (swapEyes) if (eye == Eye.LEFT) Eye.RIGHT else Eye.LEFT else eye
        return when (stereoInputMode) {
            StereoInputMode.MONO -> UvTransform()
            StereoInputMode.SBS -> if (actualEye == Eye.LEFT) UvTransform(uScale = 0.5f) else UvTransform(uOffset = 0.5f, uScale = 0.5f)
            StereoInputMode.SBS_REVERSED -> if (actualEye == Eye.LEFT) UvTransform(uOffset = 0.5f, uScale = 0.5f) else UvTransform(uScale = 0.5f)
            StereoInputMode.OU -> if (actualEye == Eye.LEFT) UvTransform(vScale = 0.5f) else UvTransform(vOffset = 0.5f, vScale = 0.5f)
            StereoInputMode.OU_REVERSED -> if (actualEye == Eye.LEFT) UvTransform(vOffset = 0.5f, vScale = 0.5f) else UvTransform(vScale = 0.5f)
            StereoInputMode.VR_CAM_V1 -> if (actualEye == Eye.RIGHT) UvTransform(uOffset = 0.0001f, uScale = 0.5f) else UvTransform(uOffset = 0.5f, uScale = 0.5f)
            StereoInputMode.VR_CAM_V2 -> if (actualEye == Eye.LEFT) UvTransform(uOffset = 0.0001f, uScale = 0.5f) else UvTransform(uOffset = 0.5f, uScale = 0.5f)
        }
    }
}
