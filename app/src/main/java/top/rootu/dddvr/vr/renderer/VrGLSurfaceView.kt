package top.rootu.dddvr.vr.renderer

import android.content.Context
import android.opengl.GLSurfaceView

class VrGLSurfaceView(
    context: Context,
    renderer: VrSceneRenderer
) : GLSurfaceView(context) {
    init {
        setEGLContextClientVersion(2)
        setRenderer(renderer)
        renderMode = RENDERMODE_CONTINUOUSLY
    }
}
