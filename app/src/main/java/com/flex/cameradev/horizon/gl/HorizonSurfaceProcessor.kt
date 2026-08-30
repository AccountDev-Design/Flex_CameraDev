package com.flex.cameradev.horizon.gl

import android.graphics.SurfaceTexture
import android.opengl.EGLSurface
import android.opengl.Matrix
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.view.Surface
import androidx.camera.core.SurfaceOutput
import androidx.camera.core.SurfaceProcessor
import androidx.camera.core.SurfaceRequest
import java.util.concurrent.Executor

/**
 * Rotates every camera frame on the GPU before CameraX hands it to the preview
 * and to the recorder, so the correction that is shown is the correction that
 * ends up in the file.
 *
 * All GL work happens on a private thread; nothing is copied through a Bitmap
 * and the matrices and buffers are allocated once.
 */
class HorizonSurfaceProcessor(
    private val correction: CorrectionSource,
) : SurfaceProcessor {

    /**
     * Read once per frame on the GL thread. Implementations must not block and
     * must not allocate.
     */
    interface CorrectionSource {
        /** Rotation to apply to the image, in degrees, counter clockwise positive. */
        fun appliedRollDegrees(): Float

        /** Uniform magnification that keeps the rotated frame covering the viewport. */
        fun renderScale(): Float
    }

    private val thread = HandlerThread("A55-HorizonGL").apply { start() }
    private val handler = Handler(thread.looper)
    private val glExecutor = Executor { command -> handler.post(command) }

    private val eglCore = EglCore()
    private val program = OesQuadProgram()

    private val surfaceTextureMatrix = FloatArray(16)
    private val outputMatrix = FloatArray(16)

    private var textureId = 0
    private var surfaceTexture: SurfaceTexture? = null
    private var inputSurface: Surface? = null

    private val outputs = LinkedHashMap<SurfaceOutput, EGLSurface>()

    @Volatile
    private var released = false

    init {
        Matrix.setIdentityM(surfaceTextureMatrix, 0)
        Matrix.setIdentityM(outputMatrix, 0)
    }

    override fun onInputSurface(request: SurfaceRequest) {
        handler.post {
            if (released || !ensureGl()) {
                request.willNotProvideSurface()
                return@post
            }
            releaseInput()
            val texture = program.createExternalTexture()
            if (texture == 0) {
                request.willNotProvideSurface()
                return@post
            }
            val surfaceTextureInstance = SurfaceTexture(texture)
            surfaceTextureInstance.setDefaultBufferSize(
                request.resolution.width,
                request.resolution.height,
            )
            surfaceTextureInstance.setOnFrameAvailableListener({ drawFrame() }, handler)
            val surface = Surface(surfaceTextureInstance)

            textureId = texture
            surfaceTexture = surfaceTextureInstance
            inputSurface = surface

            request.provideSurface(surface, glExecutor) { _ ->
                handler.post {
                    if (surfaceTexture === surfaceTextureInstance) {
                        releaseInput()
                    } else {
                        surfaceTextureInstance.release()
                        surface.release()
                        program.deleteTexture(texture)
                    }
                }
            }
        }
    }

    override fun onOutputSurface(surfaceOutput: SurfaceOutput) {
        handler.post {
            if (released || !ensureGl()) {
                surfaceOutput.close()
                return@post
            }
            val surface = surfaceOutput.getSurface(glExecutor) { _ ->
                handler.post { removeOutput(surfaceOutput) }
            }
            val eglSurface = eglCore.createWindowSurface(surface)
            if (eglSurface == null) {
                Log.e(TAG, "Sin superficie EGL para la salida ${surfaceOutput.targets}")
                surfaceOutput.close()
                return@post
            }
            outputs[surfaceOutput] = eglSurface
        }
    }

    /** Releases the pipeline; safe to call more than once. */
    fun release() {
        if (released) return
        released = true
        handler.post {
            for ((output, eglSurface) in outputs) {
                eglCore.releaseSurface(eglSurface)
                output.close()
            }
            outputs.clear()
            releaseInput()
            eglCore.makeAuxiliaryCurrent()
            program.release()
            eglCore.release()
            thread.quitSafely()
        }
    }

    private fun ensureGl(): Boolean {
        if (!eglCore.setUp()) return false
        if (!eglCore.makeAuxiliaryCurrent()) return false
        return program.create()
    }

    private fun drawFrame() {
        if (released) return
        val texture = surfaceTexture ?: return
        if (!eglCore.makeAuxiliaryCurrent()) return
        try {
            texture.updateTexImage()
        } catch (e: IllegalStateException) {
            Log.w(TAG, "updateTexImage falló", e)
            return
        }
        texture.getTransformMatrix(surfaceTextureMatrix)
        val timestamp = texture.timestamp
        val roll = correction.appliedRollDegrees()
        val scale = correction.renderScale()

        for ((output, eglSurface) in outputs) {
            if (!eglCore.makeCurrent(eglSurface)) continue
            output.updateTransformMatrix(outputMatrix, surfaceTextureMatrix)
            val size = output.size
            program.draw(textureId, outputMatrix, size.width, size.height, roll, scale)
            eglCore.setPresentationTime(eglSurface, timestamp)
            eglCore.swapBuffers(eglSurface)
        }
        eglCore.makeAuxiliaryCurrent()
    }

    private fun removeOutput(surfaceOutput: SurfaceOutput) {
        val eglSurface = outputs.remove(surfaceOutput)
        if (eglSurface != null) {
            eglCore.makeAuxiliaryCurrent()
            eglCore.releaseSurface(eglSurface)
        }
        surfaceOutput.close()
    }

    private fun releaseInput() {
        surfaceTexture?.let {
            it.setOnFrameAvailableListener(null)
            it.release()
        }
        surfaceTexture = null
        inputSurface?.release()
        inputSurface = null
        if (textureId != 0) {
            eglCore.makeAuxiliaryCurrent()
            program.deleteTexture(textureId)
            textureId = 0
        }
    }

    private companion object {
        const val TAG = "HorizonSurfaceProcessor"
    }
}
