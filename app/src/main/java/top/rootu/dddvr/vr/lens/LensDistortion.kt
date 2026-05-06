package top.rootu.dddvr.vr.lens

import top.rootu.dddvr.vr.camera.Eye

class LensDistortion(private val profile: LensProfile) {
    private var width: Int = 0
    private var height: Int = 0

    fun updateViewports(fullWidth: Int, fullHeight: Int) {
        width = fullWidth
        height = fullHeight
    }

    fun viewportFor(eye: Eye): EyeViewport {
        val half = width / 2
        return if (eye == Eye.LEFT) EyeViewport(0, 0, half, height) else EyeViewport(half, 0, half, height)
    }

    fun profile(): LensProfile = profile
}
