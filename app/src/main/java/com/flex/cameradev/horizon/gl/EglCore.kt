package com.flex.cameradev.horizon.gl

import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLExt
import android.opengl.EGLSurface
import android.util.Log
import android.view.Surface

/**
 * Minimal EGL wrapper for the horizon pipeline.
 *
 * One context is shared by every output surface, so the camera texture is
 * uploaded once per frame and only the draw call is repeated per target.
 */
class EglCore {

    private var display: EGLDisplay = EGL14.EGL_NO_DISPLAY
    private var context: EGLContext = EGL14.EGL_NO_CONTEXT
    private var config: EGLConfig? = null
    private var pbuffer: EGLSurface = EGL14.EGL_NO_SURFACE

    val isReady: Boolean get() = context != EGL14.EGL_NO_CONTEXT

    fun setUp(): Boolean {
        if (isReady) return true
        display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        if (display == EGL14.EGL_NO_DISPLAY) {
            Log.e(TAG, "eglGetDisplay falló")
            return false
        }
        val version = IntArray(2)
        if (!EGL14.eglInitialize(display, version, 0, version, 1)) {
            Log.e(TAG, "eglInitialize falló")
            display = EGL14.EGL_NO_DISPLAY
            return false
        }
        val attributes = intArrayOf(
            EGL14.EGL_RED_SIZE, 8,
            EGL14.EGL_GREEN_SIZE, 8,
            EGL14.EGL_BLUE_SIZE, 8,
            EGL14.EGL_ALPHA_SIZE, 8,
            EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
            EGL_RECORDABLE_ANDROID, 1,
            EGL14.EGL_NONE,
        )
        val configs = arrayOfNulls<EGLConfig>(1)
        val configCount = IntArray(1)
        if (!EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, configCount, 0) ||
            configCount[0] <= 0
        ) {
            Log.e(TAG, "Sin configuración EGL compatible")
            release()
            return false
        }
        config = configs[0]
        val contextAttributes = intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE)
        context = EGL14.eglCreateContext(
            display,
            config,
            EGL14.EGL_NO_CONTEXT,
            contextAttributes,
            0,
        )
        if (context == EGL14.EGL_NO_CONTEXT) {
            Log.e(TAG, "eglCreateContext falló")
            release()
            return false
        }
        val pbufferAttributes = intArrayOf(EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE)
        pbuffer = EGL14.eglCreatePbufferSurface(display, config, pbufferAttributes, 0)
        if (pbuffer == EGL14.EGL_NO_SURFACE) {
            Log.e(TAG, "No se pudo crear la superficie auxiliar")
            release()
            return false
        }
        return true
    }

    fun createWindowSurface(surface: Surface): EGLSurface? {
        if (!isReady) return null
        val attributes = intArrayOf(EGL14.EGL_NONE)
        val eglSurface = try {
            EGL14.eglCreateWindowSurface(display, config, surface, attributes, 0)
        } catch (e: IllegalArgumentException) {
            Log.e(TAG, "Superficie de ventana inválida", e)
            return null
        }
        if (eglSurface == EGL14.EGL_NO_SURFACE) {
            Log.e(TAG, "eglCreateWindowSurface falló: ${EGL14.eglGetError()}")
            return null
        }
        return eglSurface
    }

    /** Makes the auxiliary surface current, which is enough to update a texture. */
    fun makeAuxiliaryCurrent(): Boolean = makeCurrent(pbuffer)

    fun makeCurrent(eglSurface: EGLSurface?): Boolean {
        if (!isReady || eglSurface == null) return false
        if (!EGL14.eglMakeCurrent(display, eglSurface, eglSurface, context)) {
            Log.e(TAG, "eglMakeCurrent falló: ${EGL14.eglGetError()}")
            return false
        }
        return true
    }

    fun detachCurrent() {
        if (display == EGL14.EGL_NO_DISPLAY) return
        EGL14.eglMakeCurrent(
            display,
            EGL14.EGL_NO_SURFACE,
            EGL14.EGL_NO_SURFACE,
            EGL14.EGL_NO_CONTEXT,
        )
    }

    fun setPresentationTime(eglSurface: EGLSurface?, nanoseconds: Long) {
        if (!isReady || eglSurface == null) return
        EGLExt.eglPresentationTimeANDROID(display, eglSurface, nanoseconds)
    }

    fun swapBuffers(eglSurface: EGLSurface?): Boolean {
        if (!isReady || eglSurface == null) return false
        return EGL14.eglSwapBuffers(display, eglSurface)
    }

    fun releaseSurface(eglSurface: EGLSurface?) {
        if (eglSurface == null || eglSurface == EGL14.EGL_NO_SURFACE) return
        if (display == EGL14.EGL_NO_DISPLAY) return
        EGL14.eglDestroySurface(display, eglSurface)
    }

    fun release() {
        if (display != EGL14.EGL_NO_DISPLAY) {
            detachCurrent()
            releaseSurface(pbuffer)
            if (context != EGL14.EGL_NO_CONTEXT) {
                EGL14.eglDestroyContext(display, context)
            }
            EGL14.eglTerminate(display)
        }
        display = EGL14.EGL_NO_DISPLAY
        context = EGL14.EGL_NO_CONTEXT
        config = null
        pbuffer = EGL14.EGL_NO_SURFACE
    }

    private companion object {
        const val TAG = "EglCore"

        /** Marks the config as usable by the video encoder. */
        const val EGL_RECORDABLE_ANDROID = 0x3142
    }
}
