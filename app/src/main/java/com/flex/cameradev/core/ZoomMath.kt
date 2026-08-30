package com.flex.cameradev.core

import kotlin.math.abs
import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.roundToInt

/**
 * Pure zoom mathematics shared by the pinch gesture, the logarithmic slider,
 * the quick buttons and the capture pipeline.
 *
 * The class deliberately contains no Android types so that every rule here is
 * covered by JVM unit tests.
 */
object ZoomMath {

    /** Lowest ratio the UI ever exposes. */
    const val MIN_RATIO = 1.0f

    /** Highest ratio of the experimental digital zoom. */
    const val MAX_DIGITAL_RATIO = 1000.0f

    /** Ratios exposed as quick buttons. */
    val QUICK_STOPS: List<Float> = listOf(1f, 3f, 10f, 100f, 1000f)

    /** Replaces NaN / infinite values with [fallback]. */
    fun sanitize(value: Float, fallback: Float): Float =
        if (value.isNaN() || value.isInfinite()) fallback else value

    fun clamp(value: Float, min: Float, max: Float): Float {
        val lo = minOf(min, max)
        val hi = maxOf(min, max)
        val safe = sanitize(value, lo)
        return when {
            safe < lo -> lo
            safe > hi -> hi
            else -> safe
        }
    }

    /**
     * Maps a zoom ratio to a 0..1 slider position on a logarithmic scale, so
     * 1x..10x uses as much travel as 100x..1000x.
     */
    fun ratioToPosition(ratio: Float, minRatio: Float, maxRatio: Float): Float {
        val lo = sanitize(minRatio, MIN_RATIO).coerceAtLeast(0.01f)
        val hi = sanitize(maxRatio, MAX_DIGITAL_RATIO).coerceAtLeast(lo * 1.0001f)
        val safeRatio = clamp(ratio, lo, hi)
        val span = ln(hi / lo)
        if (span <= 0f) return 0f
        return (ln(safeRatio / lo) / span).coerceIn(0.0f, 1.0f)
    }

    /** Inverse of [ratioToPosition]. */
    fun positionToRatio(position: Float, minRatio: Float, maxRatio: Float): Float {
        val lo = sanitize(minRatio, MIN_RATIO).coerceAtLeast(0.01f)
        val hi = sanitize(maxRatio, MAX_DIGITAL_RATIO).coerceAtLeast(lo * 1.0001f)
        val t = clamp(position, 0f, 1f)
        return clamp(lo * (hi / lo).toDouble().pow(t.toDouble()).toFloat(), lo, hi)
    }

    /**
     * Splits a requested ratio into the part the camera hardware can honour and
     * the residual factor that has to be produced by cropping.
     */
    fun plan(requestedRatio: Float, cameraMin: Float, cameraMax: Float): ZoomPlan {
        val lo = sanitize(cameraMin, MIN_RATIO).coerceAtLeast(0.01f)
        val hi = sanitize(cameraMax, lo).coerceAtLeast(lo)
        val requested = clamp(requestedRatio, lo, MAX_DIGITAL_RATIO)
        val cameraRatio = clamp(requested, lo, hi)
        val digital = if (requested > hi) requested / hi else 1.0f
        return ZoomPlan(
            requestedRatio = requested,
            cameraRatio = cameraRatio,
            digitalFactor = clamp(digital, 1.0f, MAX_DIGITAL_RATIO),
        )
    }

    /**
     * Returns the quick stop crossed while moving from [previous] to [next],
     * or null when no stop was crossed. Used to fire a single short haptic tick
     * instead of a continuous vibration.
     */
    fun stopCrossed(previous: Float, next: Float, stops: List<Float> = QUICK_STOPS): Float? {
        val from = sanitize(previous, MIN_RATIO)
        val to = sanitize(next, MIN_RATIO)
        if (from == to) return null
        return if (to > from) {
            stops.filter { it > from && it <= to }.minOrNull()
        } else {
            stops.filter { it < from && it >= to }.maxOrNull()
        }
    }

    /** Formats a ratio the way it is rendered next to the slider: 1x, 2.5x, 100x. */
    fun formatRatio(ratio: Float): String {
        val safe = clamp(ratio, MIN_RATIO, MAX_DIGITAL_RATIO)
        val rounded = (safe * 10f).roundToInt() / 10f
        return if (abs(rounded - rounded.roundToInt()) < 0.05f || rounded >= 10f) {
            "${rounded.roundToInt()}×"
        } else {
            val whole = rounded.toInt()
            val decimal = ((rounded - whole) * 10f).roundToInt()
            "$whole.$decimal×"
        }
    }
}

/**
 * Result of [ZoomMath.plan].
 *
 * @property cameraRatio value that may be forwarded to CameraControl.setZoomRatio.
 * @property digitalFactor extra magnification produced by cropping (1.0 == none).
 */
data class ZoomPlan(
    val requestedRatio: Float,
    val cameraRatio: Float,
    val digitalFactor: Float,
) {
    val isDigital: Boolean get() = digitalFactor > 1.001f
}
