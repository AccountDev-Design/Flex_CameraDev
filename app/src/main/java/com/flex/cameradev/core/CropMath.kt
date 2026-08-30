package com.flex.cameradev.core

import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sign
import kotlin.math.tanh

/**
 * Geometry of the horizon correction: how much the frame has to be scaled up so
 * a rotated image still covers the viewport, and how much of it that costs.
 */
object CropMath {

    private const val DEG_TO_RAD = Math.PI / 180.0

    /** Scale that keeps a [width] x [height] frame covering its viewport after rotating by [rollDegrees]. */
    fun coverScale(rollDegrees: Float, width: Int, height: Int): Float {
        if (width <= 0 || height <= 0) return 1f
        val roll = HorizonMath.normalizeDegrees(rollDegrees)
        val radians = abs(roll) * DEG_TO_RAD
        val c = abs(cos(radians))
        val s = abs(sin(radians))
        val w = width.toDouble()
        val h = height.toDouble()
        val byWidth = (w * c + h * s) / w
        val byHeight = (w * s + h * c) / h
        return maxOf(1.0, maxOf(byWidth, byHeight)).toFloat()
    }

    /** Percentage of the original frame area lost to the rotation crop. */
    fun croppedAreaPercent(scale: Float): Float {
        val s = if (scale.isNaN() || scale.isInfinite()) 1f else scale.coerceAtLeast(1f)
        return ((1.0 - 1.0 / (s.toDouble() * s.toDouble())) * 100.0).toFloat().coerceIn(0f, 100f)
    }

    /**
     * Largest absolute roll that can still be corrected without exceeding
     * [maxScale]. Uses a bisection because [coverScale] is monotonic in |roll|
     * over 0..90 degrees.
     */
    fun maxCorrectableRoll(maxScale: Float, width: Int, height: Int): Float {
        if (maxScale <= 1.0001f) return 0f
        if (coverScale(90f, width, height) <= maxScale) return 90f
        var low = 0f
        var high = 90f
        repeat(24) {
            val mid = (low + high) / 2f
            if (coverScale(mid, width, height) <= maxScale) low = mid else high = mid
        }
        return low
    }

    /**
     * Applies the correction budget: small tilts are corrected one to one, and
     * the correction eases smoothly into [limitDegrees] instead of snapping,
     * which is what keeps black borders out of the frame.
     */
    fun softLimit(rollDegrees: Float, limitDegrees: Float): Float {
        val roll = HorizonMath.normalizeDegrees(rollDegrees)
        val limit = abs(limitDegrees)
        if (limit <= 0.001f) return 0f
        val magnitude = abs(roll)
        val knee = limit * 0.7f
        if (magnitude <= knee) return roll
        val remaining = limit - knee
        if (remaining <= 0.0001f) return sign(roll) * limit
        val eased = knee + remaining * tanh((magnitude - knee) / remaining)
        return sign(roll) * eased
    }

    /**
     * Full correction plan for one frame.
     *
     * @param measuredRoll roll reported by the sensors.
     * @param maxScale how much magnification the mode is allowed to spend.
     */
    fun plan(
        measuredRoll: Float,
        width: Int,
        height: Int,
        maxScale: Float,
    ): HorizonCorrection {
        val limit = maxCorrectableRoll(maxScale, width, height)
        val applied = softLimit(measuredRoll, limit)
        val scale = coverScale(applied, width, height).coerceAtMost(maxScale.coerceAtLeast(1f))
        val requested = abs(HorizonMath.normalizeDegrees(measuredRoll))
        return HorizonCorrection(
            measuredRoll = HorizonMath.normalizeDegrees(measuredRoll),
            appliedRoll = applied,
            scale = scale,
            croppedPercent = croppedAreaPercent(scale),
            atLimit = requested > limit + 0.25f,
        )
    }
}

data class HorizonCorrection(
    val measuredRoll: Float,
    val appliedRoll: Float,
    val scale: Float,
    val croppedPercent: Float,
    val atLimit: Boolean,
) {
    /** Residual tilt the correction could not absorb. */
    val residualRoll: Float get() = measuredRoll - appliedRoll
}
