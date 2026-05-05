package top.rootu.dddvr.vr.projection

import android.opengl.GLES11Ext
import android.opengl.GLES20
import top.rootu.dddvr.vr.camera.Eye
import top.rootu.dddvr.vr.renderer.VideoTextureSource
import top.rootu.dddvr.vr.stereo.StereoUvMapper

class FlatProjection : Projection(ProjectionType.FLAT) {
    private var width: Int = 1
    private var height: Int = 1
    private val program: Int
    private val positionHandle: Int
    private val uvHandle: Int
    private val samplerHandle: Int
    private val uOffsetHandle: Int
    private val vOffsetHandle: Int
    private val uScaleHandle: Int
    private val vScaleHandle: Int

    private val vertices = floatArrayOf(
        -1f, -1f, 1f, -1f, -1f, 1f,
        1f, -1f, 1f, 1f, -1f, 1f
    )
    private val uvs = floatArrayOf(
        0f, 1f, 1f, 1f, 0f, 0f,
        1f, 1f, 1f, 0f, 0f, 0f
    )

    init {
        program = createProgram(VS, FS)
        positionHandle = GLES20.glGetAttribLocation(program, "aPosition")
        uvHandle = GLES20.glGetAttribLocation(program, "aTexCoord")
        samplerHandle = GLES20.glGetUniformLocation(program, "uTexture")
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
        GLES20.glUseProgram(program)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texture.textureId)
        GLES20.glUniform1i(samplerHandle, 0)

        val t = stereoMapper.getUvTransform(eye)
        GLES20.glUniform1f(uOffsetHandle, t.uOffset)
        GLES20.glUniform1f(vOffsetHandle, t.vOffset)
        GLES20.glUniform1f(uScaleHandle, t.uScale)
        GLES20.glUniform1f(vScaleHandle, t.vScale)

        val vb = java.nio.ByteBuffer.allocateDirect(vertices.size * 4).order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer().put(vertices)
        vb.position(0)
        val ub = java.nio.ByteBuffer.allocateDirect(uvs.size * 4).order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer().put(uvs)
        ub.position(0)

        GLES20.glEnableVertexAttribArray(positionHandle)
        GLES20.glVertexAttribPointer(positionHandle, 2, GLES20.GL_FLOAT, false, 0, vb)
        GLES20.glEnableVertexAttribArray(uvHandle)
        GLES20.glVertexAttribPointer(uvHandle, 2, GLES20.GL_FLOAT, false, 0, ub)

        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, 6)
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
        const val VS = "attribute vec2 aPosition;attribute vec2 aTexCoord;varying vec2 vTexCoord;void main(){gl_Position=vec4(aPosition,0.0,1.0);vTexCoord=aTexCoord;}"
        const val FS = "#extension GL_OES_EGL_image_external : require\nprecision mediump float;varying vec2 vTexCoord;uniform samplerExternalOES uTexture;uniform float uOffset;uniform float vOffset;uniform float uScale;uniform float vScale;void main(){vec2 uv=vec2(vTexCoord.x*uScale+uOffset,vTexCoord.y*vScale+vOffset);gl_FragColor=texture2D(uTexture,uv);}"
    }
}
