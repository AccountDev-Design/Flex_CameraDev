package com.flex.cameradev.horizon.gl

import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.opengl.Matrix
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer

/**
 * Draws the camera's external texture onto a full screen quad.
 *
 * The rotation is applied to the quad vertices in an aspect corrected space, so
 * the image turns without being stretched, and the uniform scale is what keeps
 * the corners filled.
 */
class OesQuadProgram {

    private var program = 0
    private var positionHandle = 0
    private var textureCoordinateHandle = 0
    private var textureMatrixHandle = 0
    private var vertexMatrixHandle = 0
    private var samplerHandle = 0

    private val vertexMatrix = FloatArray(16)

    private val vertices: FloatBuffer = allocate(
        floatArrayOf(
            -1f, -1f,
            1f, -1f,
            -1f, 1f,
            1f, 1f,
        ),
    )

    private val textureCoordinates: FloatBuffer = allocate(
        floatArrayOf(
            0f, 0f,
            1f, 0f,
            0f, 1f,
            1f, 1f,
        ),
    )

    val isReady: Boolean get() = program != 0

    fun create(): Boolean {
        if (program != 0) return true
        val vertexShader = compile(GLES20.GL_VERTEX_SHADER, VERTEX_SHADER)
        val fragmentShader = compile(GLES20.GL_FRAGMENT_SHADER, FRAGMENT_SHADER)
        if (vertexShader == 0 || fragmentShader == 0) return false
        val handle = GLES20.glCreateProgram()
        GLES20.glAttachShader(handle, vertexShader)
        GLES20.glAttachShader(handle, fragmentShader)
        GLES20.glLinkProgram(handle)
        val status = IntArray(1)
        GLES20.glGetProgramiv(handle, GLES20.GL_LINK_STATUS, status, 0)
        GLES20.glDeleteShader(vertexShader)
        GLES20.glDeleteShader(fragmentShader)
        if (status[0] != GLES20.GL_TRUE) {
            Log.e(TAG, "Enlazado del programa fallido: " + GLES20.glGetProgramInfoLog(handle))
            GLES20.glDeleteProgram(handle)
            return false
        }
        program = handle
        positionHandle = GLES20.glGetAttribLocation(program, "aPosition")
        textureCoordinateHandle = GLES20.glGetAttribLocation(program, "aTextureCoord")
        textureMatrixHandle = GLES20.glGetUniformLocation(program, "uTexMatrix")
        vertexMatrixHandle = GLES20.glGetUniformLocation(program, "uVertexMatrix")
        samplerHandle = GLES20.glGetUniformLocation(program, "sTexture")
        return true
    }

    /** Allocates the external texture the SurfaceTexture writes into. */
    fun createExternalTexture(): Int {
        val textures = IntArray(1)
        GLES20.glGenTextures(1, textures, 0)
        val id = textures[0]
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, id)
        GLES20.glTexParameteri(
            GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_MIN_FILTER,
            GLES20.GL_LINEAR,
        )
        GLES20.glTexParameteri(
            GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_MAG_FILTER,
            GLES20.GL_LINEAR,
        )
        GLES20.glTexParameteri(
            GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_WRAP_S,
            GLES20.GL_CLAMP_TO_EDGE,
        )
        GLES20.glTexParameteri(
            GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_WRAP_T,
            GLES20.GL_CLAMP_TO_EDGE,
        )
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, 0)
        return id
    }

    fun deleteTexture(textureId: Int) {
        if (textureId == 0) return
        GLES20.glDeleteTextures(1, intArrayOf(textureId), 0)
    }

    /**
     * @param rollDegrees rotation applied to the image, already limited by the crop budget.
     * @param scale uniform magnification that keeps the corners covered.
     */
    fun draw(
        textureId: Int,
        textureMatrix: FloatArray,
        viewportWidth: Int,
        viewportHeight: Int,
        rollDegrees: Float,
        scale: Float,
    ) {
        if (!isReady || viewportWidth <= 0 || viewportHeight <= 0) return
        buildVertexMatrix(viewportWidth, viewportHeight, rollDegrees, scale)

        GLES20.glViewport(0, 0, viewportWidth, viewportHeight)
        GLES20.glClearColor(0f, 0f, 0f, 1f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
        GLES20.glUseProgram(program)

        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)
        GLES20.glUniform1i(samplerHandle, 0)
        GLES20.glUniformMatrix4fv(textureMatrixHandle, 1, false, textureMatrix, 0)
        GLES20.glUniformMatrix4fv(vertexMatrixHandle, 1, false, vertexMatrix, 0)

        vertices.position(0)
        GLES20.glVertexAttribPointer(positionHandle, 2, GLES20.GL_FLOAT, false, 0, vertices)
        GLES20.glEnableVertexAttribArray(positionHandle)

        textureCoordinates.position(0)
        GLES20.glVertexAttribPointer(
            textureCoordinateHandle,
            2,
            GLES20.GL_FLOAT,
            false,
            0,
            textureCoordinates,
        )
        GLES20.glEnableVertexAttribArray(textureCoordinateHandle)

        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)

        GLES20.glDisableVertexAttribArray(positionHandle)
        GLES20.glDisableVertexAttribArray(textureCoordinateHandle)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, 0)
    }

    /**
     * Builds A^-1 * R * A * S, where A stretches the clip space by the viewport
     * aspect ratio so the rotation is isotropic and nothing gets squashed.
     */
    internal fun buildVertexMatrix(
        viewportWidth: Int,
        viewportHeight: Int,
        rollDegrees: Float,
        scale: Float,
    ): FloatArray {
        val aspect = if (viewportHeight <= 0) 1f else viewportWidth.toFloat() / viewportHeight.toFloat()
        val safeAspect = if (aspect.isNaN() || aspect <= 0f) 1f else aspect
        val safeScale = if (scale.isNaN() || scale <= 0f) 1f else scale
        val safeRoll = if (rollDegrees.isNaN() || rollDegrees.isInfinite()) 0f else rollDegrees
        Matrix.setIdentityM(vertexMatrix, 0)
        Matrix.scaleM(vertexMatrix, 0, 1f / safeAspect, 1f, 1f)
        Matrix.rotateM(vertexMatrix, 0, safeRoll, 0f, 0f, 1f)
        Matrix.scaleM(vertexMatrix, 0, safeAspect, 1f, 1f)
        Matrix.scaleM(vertexMatrix, 0, safeScale, safeScale, 1f)
        return vertexMatrix
    }

    fun release() {
        if (program != 0) {
            GLES20.glDeleteProgram(program)
            program = 0
        }
    }

    private fun compile(type: Int, source: String): Int {
        val shader = GLES20.glCreateShader(type)
        GLES20.glShaderSource(shader, source)
        GLES20.glCompileShader(shader)
        val status = IntArray(1)
        GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, status, 0)
        if (status[0] != GLES20.GL_TRUE) {
            Log.e(TAG, "Shader no compilado: " + GLES20.glGetShaderInfoLog(shader))
            GLES20.glDeleteShader(shader)
            return 0
        }
        return shader
    }

    private fun allocate(data: FloatArray): FloatBuffer =
        ByteBuffer.allocateDirect(data.size * Float.SIZE_BYTES)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .apply {
                put(data)
                position(0)
            }

    private companion object {
        const val TAG = "OesQuadProgram"

        const val VERTEX_SHADER = """
            uniform mat4 uTexMatrix;
            uniform mat4 uVertexMatrix;
            attribute vec4 aPosition;
            attribute vec4 aTextureCoord;
            varying vec2 vTextureCoord;
            void main() {
                gl_Position = uVertexMatrix * aPosition;
                vTextureCoord = (uTexMatrix * aTextureCoord).xy;
            }
        """

        const val FRAGMENT_SHADER = """
            #extension GL_OES_EGL_image_external : require
            precision mediump float;
            varying vec2 vTextureCoord;
            uniform samplerExternalOES sTexture;
            void main() {
                gl_FragColor = texture2D(sTexture, vTextureCoord);
            }
        """
    }
}
