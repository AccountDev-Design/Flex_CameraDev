package com.flex.cameradev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin

class HorizonMathTest {

    private fun rad(degrees: Double) = degrees * Math.PI / 180.0

    @Test
    fun anglesAreNormalisedIntoAHalfOpenInterval() {
        assertEquals(0f, HorizonMath.normalizeDegrees(360f), 0.0001f)
        assertEquals(-90f, HorizonMath.normalizeDegrees(270f), 0.0001f)
        assertEquals(180f, HorizonMath.normalizeDegrees(180f), 0.0001f)
        assertEquals(180f, HorizonMath.normalizeDegrees(-180f), 0.0001f)
        assertEquals(1f, HorizonMath.normalizeDegrees(721f), 0.0001f)
        assertEquals(0f, HorizonMath.normalizeDegrees(Float.NaN), 0.0001f)
    }

    @Test
    fun deltaAcrossTheWrapPointTakesTheShortWay() {
        assertEquals(2f, HorizonMath.shortestDelta(179f, -179f), 0.0001f)
        assertEquals(-2f, HorizonMath.shortestDelta(-179f, 179f), 0.0001f)
        assertEquals(10f, HorizonMath.shortestDelta(-5f, 5f), 0.0001f)
        assertTrue(abs(HorizonMath.shortestDelta(0f, 190f)) <= 180f)
    }

    @Test
    fun interpolationNeverSpinsTheLongWayRound() {
        val midpoint = HorizonMath.lerpAngle(179f, -179f, 0.5f)
        assertEquals(180f, abs(midpoint), 0.0001f)
        assertEquals(179f, HorizonMath.lerpAngle(179f, -179f, 0f), 0.0001f)
        assertEquals(-179f, HorizonMath.lerpAngle(179f, -179f, 1f), 0.0001f)
    }

    @Test
    fun rollIsZeroWhenThePhoneIsHeldUpright() {
        // Device X = world east, device Y = world up, device Z = world south.
        val upright = floatArrayOf(
            1f, 0f, 0f,
            0f, 0f, -1f,
            0f, 1f, 0f,
        )
        assertEquals(0f, HorizonMath.rollFromRotationMatrix(upright), 0.001f)
    }

    @Test
    fun rollMatchesTheRotationAppliedToTheDevice() {
        for (angle in listOf(-75.0, -30.0, -5.0, 5.0, 30.0, 89.0)) {
            val c = cos(rad(angle)).toFloat()
            val s = sin(rad(angle)).toFloat()
            // Columns are the device axes expressed in world coordinates.
            val matrix = floatArrayOf(
                c, -s, 0f,
                0f, 0f, -1f,
                s, c, 0f,
            )
            assertEquals("angle=$angle", angle.toFloat(), HorizonMath.rollFromRotationMatrix(matrix), 0.01f)
        }
    }

    @Test
    fun aFlatPhoneReportsNoRollInsteadOfNoise() {
        val flat = floatArrayOf(1f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f)
        assertEquals(0f, HorizonMath.rollFromRotationMatrix(flat), 0.0001f)
        assertEquals(0f, HorizonMath.rollFromRotationMatrix(FloatArray(4)), 0.0001f)
    }

    @Test
    fun gravityAndRotationMatrixAgree() {
        for (angle in listOf(-40.0, -12.0, 0.0, 25.0, 61.0)) {
            val c = cos(rad(angle)).toFloat()
            val s = sin(rad(angle)).toFloat()
            val matrix = floatArrayOf(c, -s, 0f, 0f, 0f, -1f, s, c, 0f)
            val fromMatrix = HorizonMath.rollFromRotationMatrix(matrix)
            // Gravity in device coordinates is minus the third row scaled by g.
            val fromGravity = HorizonMath.rollFromGravity(-9.81f * matrix[6], -9.81f * matrix[7])
            assertEquals("angle=$angle", fromMatrix, fromGravity, 0.01f)
        }
    }

    @Test
    fun freeFallReadingsAreIgnored() {
        assertEquals(0f, HorizonMath.rollFromGravity(0.01f, -0.02f), 0.0001f)
    }

    @Test
    fun displayRotationIsSubtractedFromTheDeviceRoll() {
        assertEquals(0f, HorizonMath.compensateDisplay(90f, 90), 0.0001f)
        assertEquals(-10f, HorizonMath.compensateDisplay(80f, 90), 0.0001f)
        assertEquals(90f, HorizonMath.compensateDisplay(-180f, 90), 0.0001f)
    }

    @Test
    fun levelToleranceIsSymmetric() {
        assertTrue(HorizonMath.isLevel(0.4f))
        assertTrue(HorizonMath.isLevel(-0.9f))
        assertFalse(HorizonMath.isLevel(2.5f))
    }

    @Test
    fun smootherConvergesWithoutOvershooting() {
        val smoother = AngleSmoother(timeConstantSeconds = 0.1f)
        smoother.reset(0f)
        var last = 0f
        repeat(60) {
            last = smoother.update(20f, 0.016f)
            assertTrue("value=$last", last in -0.01f..20.01f)
        }
        assertEquals(20f, last, 0.5f)
    }

    @Test
    fun smootherCrossesTheWrapPointWithoutASpin() {
        val smoother = AngleSmoother(timeConstantSeconds = 0.05f)
        smoother.reset(178f)
        var previous = 178f
        repeat(30) {
            val next = smoother.update(-178f, 0.016f)
            assertTrue("jumped from $previous to $next", abs(HorizonMath.shortestDelta(previous, next)) < 20f)
            previous = next
        }
        assertEquals(0f, abs(HorizonMath.shortestDelta(previous, -178f)), 1.5f)
    }

    @Test
    fun smootherHandlesTimeGapsAndZeroDeltas() {
        val smoother = AngleSmoother(0.1f)
        smoother.reset(0f)
        assertEquals(0f, smoother.update(30f, 0f), 0.0001f)
        val afterLongGap = smoother.update(30f, 10f)
        assertEquals(30f, afterLongGap, 1f)
    }

    @Test
    fun complementaryFilterFollowsTheGyroscopeInTheShortTerm() {
        val estimator = ComplementaryRollEstimator(driftCorrectionSeconds = 2f)
        estimator.reset(0f)
        var value = 0f
        repeat(10) { value = estimator.update(30f, null, 0.01f) }
        assertEquals(3f, value, 0.2f)
    }

    @Test
    fun complementaryFilterRemovesGyroscopeDrift() {
        val estimator = ComplementaryRollEstimator(driftCorrectionSeconds = 0.5f)
        estimator.reset(0f)
        var value = 0f
        // A constantly biased gyroscope with the accelerometer reporting level.
        repeat(2000) { value = estimator.update(2f, 0f, 0.01f) }
        assertTrue("drift not bounded: $value", abs(value) < 2.5f)
    }

    @Test
    fun complementaryFilterRejectsInvalidGyroSamples() {
        val estimator = ComplementaryRollEstimator()
        estimator.reset(10f)
        val value = estimator.update(Float.NaN, null, 0.02f)
        assertEquals(10f, value, 0.0001f)
    }
}
