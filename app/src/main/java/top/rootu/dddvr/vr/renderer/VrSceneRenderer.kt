package top.rootu.dddvr.vr.renderer

import android.graphics.SurfaceTexture
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.view.Surface
import top.rootu.dddvr.vr.camera.Eye
import top.rootu.dddvr.vr.input.VrInputController
import top.rootu.dddvr.vr.projection.ProjectionManager
import top.rootu.dddvr.vr.stereo.StereoUvMapper
import top.rootu.dddvr.vr.ui.VrUiLayer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class VrSceneRenderer(
    private val onSurfaceReady: (Surface) -> Unit
) : GLSurfaceView.Renderer, SurfaceTexture.OnFrameAvailableListener {
    lateinit var projectionManager: ProjectionManager
        private set
    val uiLayer = VrUiLayer()
    val inputController = VrInputController()

    private lateinit var textureSource: VideoTextureSource
    private val stereoUvMapper = StereoUvMapper()
    @Volatile private var frameAvailable = false

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        val textureId = IntArray(1)
        GLES20.glGenTextures(1, textureId, 0)
        val surfaceTexture = SurfaceTexture(textureId[0])
        surfaceTexture.setOnFrameAvailableListener(this)
        val surface = Surface(surfaceTexture)
        textureSource = VideoTextureSource(surfaceTexture, surface, textureId[0])
        projectionManager = ProjectionManager(textureSource)
        onSurfaceReady(surface)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        GLES20.glViewport(0, 0, width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        if (frameAvailable) {
            textureSource.updateTexImage()
            frameAvailable = false
        }
        projectionManager.renderEye(Eye.LEFT, stereoUvMapper)
        projectionManager.renderEye(Eye.RIGHT, stereoUvMapper)
        uiLayer.update()
        inputController.update()
    }

    override fun onFrameAvailable(surfaceTexture: SurfaceTexture?) {
        frameAvailable = true
    }
}
