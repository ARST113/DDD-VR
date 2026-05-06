package top.rootu.dddvr.xr.bridge

import android.graphics.SurfaceTexture
import android.view.Surface

class OpenXrVideoSurface(textureId: Int) {
    private val surfaceTexture = SurfaceTexture(textureId)
    val surface: Surface = Surface(surfaceTexture)

    fun updateTexImage() {
        surfaceTexture.updateTexImage()
    }

    fun getTransformMatrix(): FloatArray {
        val matrix = FloatArray(16)
        surfaceTexture.getTransformMatrix(matrix)
        return matrix
    }

    fun release() {
        surface.release()
        surfaceTexture.release()
    }
}
