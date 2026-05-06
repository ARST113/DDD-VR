package top.rootu.dddvr.vr.render

import android.graphics.SurfaceTexture
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.view.Surface

class ExternalVideoTexture {
    val textureId: Int
    val surfaceTexture: SurfaceTexture
    val surface: Surface

    init {
        val ids = IntArray(1)
        GLES20.glGenTextures(1, ids, 0)
        textureId = ids[0]
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)
        surfaceTexture = SurfaceTexture(textureId)
        surface = Surface(surfaceTexture)
    }
}
