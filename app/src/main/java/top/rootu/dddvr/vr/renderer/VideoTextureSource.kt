package top.rootu.dddvr.vr.renderer

import android.graphics.SurfaceTexture
import android.view.Surface

class VideoTextureSource(
    val surfaceTexture: SurfaceTexture,
    val surface: Surface,
    val textureId: Int
) {
    fun updateTexImage() = surfaceTexture.updateTexImage()

    fun release() {
        surface.release()
        surfaceTexture.release()
    }
}
