package top.rootu.dddvr.vr.renderer

import android.graphics.SurfaceTexture
import android.opengl.Matrix
import android.view.Surface

class VideoTextureSource(
    val surfaceTexture: SurfaceTexture,
    val surface: Surface,
    val textureId: Int
) {
    val transformMatrix: FloatArray = FloatArray(16).also {
        Matrix.setIdentityM(it, 0)
    }

    fun updateTexImage() {
        surfaceTexture.updateTexImage()
        surfaceTexture.getTransformMatrix(transformMatrix)
    }

    fun release() {
        surface.release()
        surfaceTexture.release()
    }
}
