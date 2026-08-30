package com.flex.cameradev.camera

import android.util.Log
import androidx.camera.core.Camera
import com.flex.cameradev.core.ZoomMath
import com.flex.cameradev.core.ZoomPlan

/**
 * Keeps CameraX, the pinch gesture, the slider and the quick buttons on the
 * same value.
 *
 * Requests above the hardware limit are split: the camera receives its maximum
 * and the remainder becomes a crop factor the rest of the app applies.
 */
class ZoomController {

    private var camera: Camera? = null

    var minRatio: Float = 1f
        private set

    var maxCameraRatio: Float = 1f
        private set

    var currentPlan: ZoomPlan = ZoomPlan(1f, 1f, 1f)
        private set

    fun attach(camera: Camera?) {
        this.camera = camera
        val zoomState = camera?.cameraInfo?.zoomState?.value
        minRatio = ZoomMath.sanitize(zoomState?.minZoomRatio ?: 1f, 1f).coerceAtLeast(0.1f)
        maxCameraRatio = ZoomMath.sanitize(zoomState?.maxZoomRatio ?: 1f, 1f)
            .coerceAtLeast(minRatio)
    }

    fun detach() {
        camera = null
    }

    /**
     * Applies [requestedRatio] and returns the resulting split. The hardware
     * only ever receives a value inside the range it reported.
     */
    fun apply(requestedRatio: Float): ZoomPlan {
        val plan = ZoomMath.plan(requestedRatio, minRatio, maxCameraRatio)
        currentPlan = plan
        val control = camera?.cameraControl
        if (control != null) {
            try {
                control.setZoomRatio(plan.cameraRatio)
            } catch (e: IllegalArgumentException) {
                Log.w(TAG, "La cámara rechazó el factor ${plan.cameraRatio}", e)
            }
        }
        return plan
    }

    /** Re-applies the current request after a rebind, clamped to the new limits. */
    fun reapply(): ZoomPlan = apply(currentPlan.requestedRatio)

    /**
     * Clamps a request to what the current mode allows; high speed sessions have
     * no crop pipeline, so they stop at the hardware maximum.
     */
    fun clampToMode(requestedRatio: Float, allowDigital: Boolean): Float {
        val ceiling = if (allowDigital) ZoomMath.MAX_DIGITAL_RATIO else maxCameraRatio
        return ZoomMath.clamp(requestedRatio, minRatio, ceiling)
    }

    private companion object {
        const val TAG = "ZoomController"
    }
}
