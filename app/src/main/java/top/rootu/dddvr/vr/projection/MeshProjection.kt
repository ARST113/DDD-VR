package top.rootu.dddvr.vr.projection

import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.opengl.Matrix
import top.rootu.dddvr.vr.camera.Eye
import top.rootu.dddvr.vr.mesh.ProjectionMesh
import top.rootu.dddvr.vr.renderer.VideoTextureSource
import top.rootu.dddvr.vr.stereo.StereoUvMapper
import java.nio.ByteBuffer
import java.nio.ByteOrder

class MeshProjection(type: ProjectionType, mesh: ProjectionMesh) : Projection(type) {
    private var width: Int = 1
    private var height: Int = 1
    private val program: Int
    private val positionHandle: Int
    private val uvHandle: Int
    private val mvpHandle: Int
    private val samplerHandle: Int
    private val texMatrixHandle: Int
    private val uOffsetHandle: Int
    private val vOffsetHandle: Int
    private val uScaleHandle: Int
    private val vScaleHandle: Int

    private val vertexBuffer = ByteBuffer.allocateDirect(mesh.vertices.size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply {
        put(mesh.vertices)
        position(0)
    }
    private val uvBuffer = ByteBuffer.allocateDirect(mesh.texCoords.size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply {
        put(mesh.texCoords)
        position(0)
    }
    private val indexBuffer = ByteBuffer.allocateDirect(mesh.indices.size * 2).order(ByteOrder.nativeOrder()).asShortBuffer().apply {
        put(mesh.indices)
        position(0)
    }
    private val indexCount = mesh.indices.size

    init {
        program = createProgram(VS, FS)
        positionHandle = GLES20.glGetAttribLocation(program, "aPosition")
        uvHandle = GLES20.glGetAttribLocation(program, "aTexCoord")
        mvpHandle = GLES20.glGetUniformLocation(program, "uMvp")
        samplerHandle = GLES20.glGetUniformLocation(program, "uTexture")
        texMatrixHandle = GLES20.glGetUniformLocation(program, "uTexMatrix")
        uOffsetHandle = GLES20.glGetUniformLocation(program, "uOffset")
        vOffsetHandle = GLES20.glGetUniformLocation(program, "vOffset")
        uScaleHandle = GLES20.glGetUniformLocation(program, "uScale")
        vScaleHandle = GLES20.glGetUniformLocation(program, "vScale")
    }

    override fun updateAspectRatio(width: Int, height: Int) {
        this.width = width.coerceAtLeast(1)
        this.height = height.coerceAtLeast(1)
    }

    override fun render(eye: Eye, texture: VideoTextureSource, stereoMapper: StereoUvMapper) {
        val half = width / 2
        if (eye == Eye.LEFT) GLES20.glViewport(0, 0, half, height) else GLES20.glViewport(half, 0, width - half, height)

        val view = FloatArray(16)
        val proj = FloatArray(16)
        val mvp = FloatArray(16)
        Matrix.setIdentityM(view, 0)
        Matrix.multiplyMM(view, 0, headPoseMatrix, 0, view, 0)
        Matrix.perspectiveM(proj, 0, 90f, (half.toFloat() / height).coerceAtLeast(0.01f), 0.1f, 100f)
        Matrix.multiplyMM(mvp, 0, proj, 0, view, 0)

        GLES20.glUseProgram(program)
        GLES20.glUniformMatrix4fv(mvpHandle, 1, false, mvp, 0)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texture.textureId)
        GLES20.glUniform1i(samplerHandle, 0)
        GLES20.glUniformMatrix4fv(texMatrixHandle, 1, false, texture.transformMatrix, 0)

        val t = stereoMapper.getUvTransform(eye)
        GLES20.glUniform1f(uOffsetHandle, t.uOffset)
        GLES20.glUniform1f(vOffsetHandle, t.vOffset)
        GLES20.glUniform1f(uScaleHandle, t.uScale)
        GLES20.glUniform1f(vScaleHandle, t.vScale)

        GLES20.glEnableVertexAttribArray(positionHandle)
        GLES20.glVertexAttribPointer(positionHandle, 3, GLES20.GL_FLOAT, false, 0, vertexBuffer)
        GLES20.glEnableVertexAttribArray(uvHandle)
        GLES20.glVertexAttribPointer(uvHandle, 2, GLES20.GL_FLOAT, false, 0, uvBuffer)

        indexBuffer.position(0)
        GLES20.glDrawElements(GLES20.GL_TRIANGLES, indexCount, GLES20.GL_UNSIGNED_SHORT, indexBuffer)
    }

    private fun createProgram(vs: String, fs: String): Int {
        fun shader(type: Int, src: String): Int = GLES20.glCreateShader(type).also {
            GLES20.glShaderSource(it, src)
            GLES20.glCompileShader(it)
        }
        val v = shader(GLES20.GL_VERTEX_SHADER, vs)
        val f = shader(GLES20.GL_FRAGMENT_SHADER, fs)
        return GLES20.glCreateProgram().also {
            GLES20.glAttachShader(it, v)
            GLES20.glAttachShader(it, f)
            GLES20.glLinkProgram(it)
        }
    }

    private companion object {
        const val VS = "attribute vec3 aPosition;attribute vec2 aTexCoord;uniform mat4 uMvp;varying vec2 vTexCoord;void main(){gl_Position=uMvp*vec4(aPosition,1.0);vTexCoord=aTexCoord;}"
        const val FS = "#extension GL_OES_EGL_image_external : require\nprecision mediump float;varying vec2 vTexCoord;uniform samplerExternalOES uTexture;uniform mat4 uTexMatrix;uniform float uOffset;uniform float vOffset;uniform float uScale;uniform float vScale;void main(){vec2 mapped=vec2(vTexCoord.x*uScale+uOffset,vTexCoord.y*vScale+vOffset);vec2 uv=(uTexMatrix*vec4(mapped,0.0,1.0)).xy;gl_FragColor=texture2D(uTexture,uv);}"
    }
}
