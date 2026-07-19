package top.rootu.dddvr.xr.bridge

import android.graphics.SurfaceTexture
import android.os.Handler
import android.os.Looper
import android.view.Surface

class OpenXrVideoSurface(textureId: Int) : SurfaceTexture.OnFrameAvailableListener {
    private val surfaceTexture = SurfaceTexture(textureId)
    @Volatile
    private var frameAvailable = false

    val surface: Surface = Surface(surfaceTexture)

    init {
        surfaceTexture.setOnFrameAvailableListener(this, Handler(Looper.getMainLooper()))
    }

    override fun onFrameAvailable(surfaceTexture: SurfaceTexture?) {
        frameAvailable = true
    }

    fun updateTexImage(transformMatrix: FloatArray): Boolean {
        if (!frameAvailable) return false
        return runCatching {
            frameAvailable = false
            surfaceTexture.updateTexImage()
            surfaceTexture.getTransformMatrix(transformMatrix)
            true
        }.getOrDefault(false)
    }

    fun timestampNs(): Long = surfaceTexture.timestamp

    fun setDefaultBufferSize(width: Int, height: Int) {
        if (width > 0 && height > 0) {
            surfaceTexture.setDefaultBufferSize(width, height)
        }
    }

    fun release() {
        surface.release()
        surfaceTexture.release()
    }
}
