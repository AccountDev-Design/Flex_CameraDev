package com.flex.cameradev.horizon

import android.content.Context
import com.flex.cameradev.core.CropMath
import com.flex.cameradev.core.HorizonCompatibility
import com.flex.cameradev.core.HorizonCorrection
import com.flex.cameradev.core.HorizonMode
import com.flex.cameradev.core.HorizonSupport
import com.flex.cameradev.core.SizeSpec
import com.flex.cameradev.horizon.gl.HorizonSurfaceProcessor

/**
 * Converts the IMU roll into the correction the renderer applies and the values
 * the overlay draws.
 *
 * The GL thread reads [appliedRollDegrees] and [renderScale] once per frame, so
 * both are plain volatile floats and the computation happens on the sensor
 * thread instead.
 */
class HorizonController(
    context: Context,
    private val onCorrection: (HorizonCorrection) -> Unit,
) : HorizonSurfaceProcessor.CorrectionSource {

    private val sensors = SensorFusionManager(context)

    @Volatile
    private var appliedRoll = 0f

    @Volatile
    private var scale = 1f

    @Volatile
    private var mode: HorizonMode = HorizonMode.OFF

    @Volatile
    private var support: HorizonSupport = HorizonCompatibility.photoSupport()

    @Volatile
    private var frameSize: SizeSpec = SizeSpec(1920, 1080)

    @Volatile
    private var digitalZoom = 1f

    /** Guards the interface callback; the renderer is never throttled. */
    private var lastNotifiedNanos = 0L

    val isSensorAvailable: Boolean get() = sensors.isAvailable

    val measuredRoll: Float get() = sensors.currentRoll

    var displayRotationDegrees: Int
        get() = sensors.displayRotationDegrees
        set(value) {
            sensors.displayRotationDegrees = value
        }

    fun start() {
        if (!sensors.isAvailable) return
        sensors.start { roll -> handleRoll(roll) }
    }

    fun stop() {
        sensors.stop()
        appliedRoll = 0f
        scale = 1f
    }

    fun setMode(mode: HorizonMode, support: HorizonSupport) {
        this.support = support
        this.mode = support.resolve(mode)
        if (this.mode == HorizonMode.OFF) {
            appliedRoll = 0f
            scale = 1f
        }
        handleRoll(sensors.currentRoll)
    }

    fun setFrameSize(size: SizeSpec) {
        if (size.width > 0 && size.height > 0) {
            frameSize = size
        }
    }

    /** Extra magnification folded into the same draw call as the rotation. */
    fun setDigitalZoom(factor: Float) {
        digitalZoom = if (factor.isNaN() || factor < 1f) 1f else factor
        handleRoll(sensors.currentRoll)
    }

    private fun handleRoll(measured: Float) {
        val activeMode = mode
        val correction = if (activeMode == HorizonMode.OFF) {
            HorizonCorrection(
                measuredRoll = measured,
                appliedRoll = 0f,
                scale = 1f,
                croppedPercent = 0f,
                atLimit = false,
            )
        } else {
            CropMath.plan(
                measuredRoll = measured,
                width = frameSize.width,
                height = frameSize.height,
                maxScale = HorizonCompatibility.maxScaleFor(support, activeMode),
            )
        }
        appliedRoll = correction.appliedRoll
        scale = correction.scale * digitalZoom
        notifyThrottled(correction)
    }

    /**
     * The renderer reads the volatile fields every frame, so the interface only
     * needs a rate that looks smooth on screen.
     */
    private fun notifyThrottled(correction: HorizonCorrection) {
        val now = System.nanoTime()
        if (now - lastNotifiedNanos < UI_INTERVAL_NANOS && lastNotifiedNanos != 0L) return
        lastNotifiedNanos = now
        onCorrection(correction)
    }

    override fun appliedRollDegrees(): Float = appliedRoll

    override fun renderScale(): Float = scale

    private companion object {
        /** Around 30 updates per second. */
        const val UI_INTERVAL_NANOS = 33_000_000L
    }
}
