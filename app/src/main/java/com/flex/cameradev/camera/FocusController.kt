package com.flex.cameradev.camera

import android.util.Log
import androidx.camera.core.Camera
import androidx.camera.core.FocusMeteringAction
import androidx.camera.core.FocusMeteringResult
import androidx.camera.core.MeteringPointFactory
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import java.util.concurrent.ExecutionException
import java.util.concurrent.TimeUnit

/**
 * Tap to focus with exposure metering.
 *
 * When the preview is magnified by the experimental digital zoom the touch has
 * to be mapped back into the unscaled preview before it is turned into a
 * metering point, otherwise the camera focuses on the wrong part of the scene.
 */
class FocusController(private val previewView: PreviewView) {

    fun interface Callback {
        fun onResult(successful: Boolean)
    }

    /**
     * @param digitalFactor magnification currently applied to the preview view.
     */
    fun focusAt(
        camera: Camera?,
        touchX: Float,
        touchY: Float,
        digitalFactor: Float,
        callback: Callback,
    ): Boolean {
        val control = camera?.cameraControl ?: return false
        val factory: MeteringPointFactory = previewView.meteringPointFactory
        val corrected = correctForDigitalZoom(touchX, touchY, digitalFactor)
        val point = factory.createPoint(corrected.first, corrected.second)
        val action = FocusMeteringAction.Builder(point, FocusMeteringAction.FLAG_AF)
            .addPoint(point, FocusMeteringAction.FLAG_AE)
            .setAutoCancelDuration(AUTO_CANCEL_SECONDS, TimeUnit.SECONDS)
            .build()
        return try {
            val future = control.startFocusAndMetering(action)
            future.addListener(
                {
                    val successful = try {
                        (future.get() as? FocusMeteringResult)?.isFocusSuccessful ?: false
                    } catch (e: ExecutionException) {
                        Log.w(TAG, "El enfoque no se completó", e)
                        false
                    } catch (e: InterruptedException) {
                        Thread.currentThread().interrupt()
                        false
                    }
                    callback.onResult(successful)
                },
                ContextCompat.getMainExecutor(previewView.context),
            )
            true
        } catch (e: IllegalArgumentException) {
            Log.w(TAG, "Punto de enfoque inválido", e)
            false
        }
    }

    /** Undoes the view magnification so the point lands on the real preview pixel. */
    internal fun correctForDigitalZoom(
        touchX: Float,
        touchY: Float,
        digitalFactor: Float,
    ): Pair<Float, Float> {
        val factor = if (digitalFactor.isNaN() || digitalFactor < 1f) 1f else digitalFactor
        if (factor <= 1.001f) return touchX to touchY
        val centerX = previewView.width / 2f
        val centerY = previewView.height / 2f
        return (centerX + (touchX - centerX) / factor) to (centerY + (touchY - centerY) / factor)
    }

    fun cancel(camera: Camera?) {
        try {
            camera?.cameraControl?.cancelFocusAndMetering()
        } catch (e: IllegalStateException) {
            Log.w(TAG, "No se pudo cancelar el enfoque", e)
        }
    }

    private companion object {
        const val TAG = "FocusController"
        const val AUTO_CANCEL_SECONDS = 4L
    }
}
