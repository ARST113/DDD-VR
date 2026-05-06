package top.rootu.dddvr.vr.renderer

import android.graphics.SurfaceTexture
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.view.Surface
import top.rootu.dddvr.vr.camera.Eye
import top.rootu.dddvr.vr.projection.FlatProjection
import top.rootu.dddvr.vr.projection.ProjectionManager
import top.rootu.dddvr.vr.projection.ProjectionType
import top.rootu.dddvr.vr.stereo.StereoUvMapper
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class VrSceneRenderer(
    private val onSurfaceReady: (Surface) -> Unit
) : GLSurfaceView.Renderer, SurfaceTexture.OnFrameAvailableListener {
    lateinit var projectionManager: ProjectionManager
        private set
    private lateinit var textureSource: VideoTextureSource
    val stereoUvMapper = StereoUvMapper()
    @Volatile private var frameAvailable = false

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        val textureId = IntArray(1)
        GLES20.glGenTextures(1, textureId, 0)
        val surfaceTexture = SurfaceTexture(textureId[0])
        surfaceTexture.setOnFrameAvailableListener(this)
        val surface = Surface(surfaceTexture)
        textureSource = VideoTextureSource(surfaceTexture, surface, textureId[0])
        projectionManager = ProjectionManager(textureSource).apply {
            register(ProjectionType.FLAT, FlatProjection())
            setCurrentProjectionType(ProjectionType.FLAT)
        }
        onSurfaceReady(surface)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        projectionManager.setAspectRatio(width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        GLES20.glClearColor(0f, 0f, 0f, 1f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
        if (frameAvailable) {
            textureSource.updateTexImage()
            frameAvailable = false
        }
        projectionManager.renderEye(Eye.LEFT, stereoUvMapper)
        projectionManager.renderEye(Eye.RIGHT, stereoUvMapper)
    }

    override fun onFrameAvailable(surfaceTexture: SurfaceTexture?) {
        frameAvailable = true
    }
}
