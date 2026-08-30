package com.flex.cameradev.core

import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.exp
import kotlin.math.hypot

/**
 * Angle arithmetic for the horizon feature.
 *
 * Everything here is pure so the +-180 degree wrap-around, the smoothing and the
 * gyroscope drift correction can be tested without a device.
 */
object HorizonMath {

    private const val RAD_TO_DEG = 180.0 / Math.PI

    /** Normalises any angle into the (-180, 180] interval. */
    fun normalizeDegrees(degrees: Float): Float {
        if (degrees.isNaN() || degrees.isInfinite()) return 0f
        var value = degrees % 360f
        if (value <= -180f) value += 360f
        if (value > 180f) value -= 360f
        // A value of exactly -180 maps to +180 to keep the interval half open.
        return if (value == -180f) 180f else value
    }

    /** Shortest signed rotation from [from] to [to]; never larger than 180 degrees. */
    fun shortestDelta(from: Float, to: Float): Float = normalizeDegrees(to - from)

    /** Interpolates along the shortest arc, so 179 -> -179 moves 2 degrees, not 358. */
    fun lerpAngle(from: Float, to: Float, fraction: Float): Float {
        val t = fraction.coerceIn(0f, 1f)
        return normalizeDegrees(from + shortestDelta(from, to) * t)
    }

    /**
     * Roll of the device around the viewing axis, derived from the rotation
     * matrix produced by SensorManager.getRotationMatrixFromVector.
     *
     * The matrix maps device coordinates to world coordinates, so the third row
     * holds the world "up" axis expressed in device coordinates.
     */
    fun rollFromRotationMatrix(matrix: FloatArray): Float {
        if (matrix.size < 9) return 0f
        val x = matrix[6]
        val y = matrix[7]
        if (hypot(x, y) < 1e-4f) return 0f
        return normalizeDegrees((atan2(x, y) * RAD_TO_DEG).toFloat())
    }

    /** Roll derived from a gravity/accelerometer reading in device coordinates. */
    fun rollFromGravity(x: Float, y: Float): Float {
        if (hypot(x, y) < 0.15f) return 0f
        return normalizeDegrees((atan2(-x, -y) * RAD_TO_DEG).toFloat())
    }

    /**
     * Compensates for the display rotation so a portrait-locked UI still reports
     * the tilt the user sees.
     *
     * @param displayRotationDegrees 0, 90, 180 or 270 as reported by Display.getRotation().
     */
    fun compensateDisplay(rollDegrees: Float, displayRotationDegrees: Int): Float =
        normalizeDegrees(rollDegrees - displayRotationDegrees.toFloat())

    /** True when the device is close enough to level to show the "levelled" state. */
    fun isLevel(rollDegrees: Float, toleranceDegrees: Float = 1.0f): Boolean =
        abs(normalizeDegrees(rollDegrees)) <= abs(toleranceDegrees)
}

/**
 * Exponential angle smoother with a time constant expressed in seconds, so the
 * result does not depend on the sensor delivery rate.
 */
class AngleSmoother(
    private val timeConstantSeconds: Float = 0.12f,
) {
    private var value = 0f
    private var initialised = false

    val current: Float get() = value

    fun reset(initial: Float = 0f) {
        value = HorizonMath.normalizeDegrees(initial)
        initialised = true
    }

    fun update(target: Float, deltaSeconds: Float): Float {
        val safeTarget = HorizonMath.normalizeDegrees(target)
        if (!initialised) {
            value = safeTarget
            initialised = true
            return value
        }
        val dt = deltaSeconds.coerceIn(0f, 0.5f)
        val tau = timeConstantSeconds.coerceAtLeast(0.001f)
        val alpha = 1f - exp(-dt / tau)
        value = HorizonMath.lerpAngle(value, safeTarget, alpha)
        return value
    }
}

/**
 * Complementary filter used when the fused rotation vector is unavailable.
 *
 * The gyroscope supplies the fast component and the accelerometer pulls the
 * estimate back towards gravity, which removes the drift a naive integration
 * of the gyroscope would accumulate.
 */
class ComplementaryRollEstimator(
    private val driftCorrectionSeconds: Float = 1.5f,
) {
    private var roll = 0f
    private var initialised = false

    val current: Float get() = roll

    fun reset(initial: Float = 0f) {
        roll = HorizonMath.normalizeDegrees(initial)
        initialised = true
    }

    /**
     * @param gyroRateDegPerSec rotation rate around the device Z axis.
     * @param accelerometerRoll roll implied by the gravity vector, or null when
     *   the accelerometer reading is unusable (free fall, strong shake).
     */
    fun update(
        gyroRateDegPerSec: Float,
        accelerometerRoll: Float?,
        deltaSeconds: Float,
    ): Float {
        val dt = deltaSeconds.coerceIn(0f, 0.5f)
        if (!initialised) {
            roll = HorizonMath.normalizeDegrees(accelerometerRoll ?: 0f)
            initialised = true
            return roll
        }
        val rate = if (gyroRateDegPerSec.isNaN() || gyroRateDegPerSec.isInfinite()) 0f else gyroRateDegPerSec
        roll = HorizonMath.normalizeDegrees(roll + rate * dt)
        if (accelerometerRoll != null) {
            val tau = driftCorrectionSeconds.coerceAtLeast(0.01f)
            val weight = (dt / (tau + dt)).coerceIn(0f, 1f)
            roll = HorizonMath.lerpAngle(roll, accelerometerRoll, weight)
        }
        return roll
    }
}
