package top.rootu.dddvr.vr.renderer

import android.graphics.SurfaceTexture
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.util.Log
import android.view.Surface
import top.rootu.dddvr.vr.camera.Eye
import top.rootu.dddvr.vr.mesh.SphereMeshFactory
import top.rootu.dddvr.vr.pose.HeadPoseProvider
import top.rootu.dddvr.vr.projection.FlatProjection
import top.rootu.dddvr.vr.projection.MeshProjection
import top.rootu.dddvr.vr.projection.ProjectionManager
import top.rootu.dddvr.vr.projection.ProjectionType
import top.rootu.dddvr.vr.stereo.StereoUvMapper
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class VrSceneRenderer(
    private val onSurfaceReady: (Surface) -> Unit,
    private val poseProvider: HeadPoseProvider? = null,
    private val initialProjectionType: ProjectionType = ProjectionType.EQUIRECT_360
) : GLSurfaceView.Renderer, SurfaceTexture.OnFrameAvailableListener {
    lateinit var projectionManager: ProjectionManager
        private set
    private lateinit var textureSource: VideoTextureSource
    val stereoUvMapper = StereoUvMapper()
    @Volatile private var frameAvailable = false
    @Volatile var framesAvailable: Long = 0L
        private set
    @Volatile var framesRendered: Long = 0L
        private set
    @Volatile private var videoWidth: Int = 0
    @Volatile private var videoHeight: Int = 0
    private val headMatrix = FloatArray(16)

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        GLES20.glDisable(GLES20.GL_CULL_FACE)
        GLES20.glDisable(GLES20.GL_DEPTH_TEST)
        val textureId = IntArray(1)
        GLES20.glGenTextures(1, textureId, 0)
        configureExternalVideoTexture(textureId[0])
        val surfaceTexture = SurfaceTexture(textureId[0])
        applyDefaultBufferSize(surfaceTexture)
        surfaceTexture.setOnFrameAvailableListener(this)
        val surface = Surface(surfaceTexture)
        textureSource = VideoTextureSource(surfaceTexture, surface, textureId[0])
        projectionManager = ProjectionManager(textureSource).apply {
            register(ProjectionType.FLAT, FlatProjection())
            register(ProjectionType.EQUIRECT_180, MeshProjection(ProjectionType.EQUIRECT_180, SphereMeshFactory.createEquirect180()))
            register(ProjectionType.EQUIRECT_360, MeshProjection(ProjectionType.EQUIRECT_360, SphereMeshFactory.createEquirect360()))
            setCurrentProjectionType(initialProjectionType)
        }
        onSurfaceReady(surface)
        Log.i(TAG, "VR_RENDER_SURFACE_CREATED texture=${textureId[0]} surface=$surface")
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
            framesRendered += 1
            if (framesRendered <= FRAME_LOG_INITIAL_COUNT || framesRendered % FRAME_LOG_INTERVAL == 0L) {
                Log.i(TAG, "VR_RENDER_FRAME_RENDERED framesAvailable=$framesAvailable framesRendered=$framesRendered")
            }
        }
        poseProvider?.getHeadMatrix(headMatrix)
        projectionManager.setHeadMatrix(headMatrix)
        projectionManager.renderEye(Eye.LEFT, stereoUvMapper)
        projectionManager.renderEye(Eye.RIGHT, stereoUvMapper)
    }

    override fun onFrameAvailable(surfaceTexture: SurfaceTexture?) {
        frameAvailable = true
        framesAvailable += 1
        if (framesAvailable <= FRAME_LOG_INITIAL_COUNT || framesAvailable % FRAME_LOG_INTERVAL == 0L) {
            Log.i(TAG, "VR_RENDER_FRAME_AVAILABLE framesAvailable=$framesAvailable framesRendered=$framesRendered")
        }
    }

    fun setVideoSize(width: Int, height: Int) {
        val safeWidth = width.coerceAtLeast(1)
        val safeHeight = height.coerceAtLeast(1)
        if (videoWidth == safeWidth && videoHeight == safeHeight) return

        videoWidth = safeWidth
        videoHeight = safeHeight

        if (::textureSource.isInitialized) {
            textureSource.surfaceTexture.setDefaultBufferSize(safeWidth, safeHeight)
            Log.i(TAG, "VR_RENDER_VIDEO_SIZE width=$safeWidth height=$safeHeight")
        }
    }

    private fun configureExternalVideoTexture(textureId: Int) {
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
    }

    private fun applyDefaultBufferSize(surfaceTexture: SurfaceTexture) {
        val width = videoWidth
        val height = videoHeight
        if (width > 0 && height > 0) {
            surfaceTexture.setDefaultBufferSize(width, height)
            Log.i(TAG, "VR_RENDER_APPLY_PENDING_VIDEO_SIZE width=$width height=$height")
        }
    }

    private companion object {
        const val TAG = "DDDVR/VrRenderer"
        const val FRAME_LOG_INITIAL_COUNT = 3
        const val FRAME_LOG_INTERVAL = 120L
    }
}
