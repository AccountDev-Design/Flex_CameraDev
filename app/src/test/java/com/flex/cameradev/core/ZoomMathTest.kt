package com.flex.cameradev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ZoomMathTest {

    @Test
    fun positionRoundTripsAcrossTheWholeRange() {
        val ratios = listOf(1f, 1.7f, 3f, 10f, 42f, 100f, 1000f)
        for (ratio in ratios) {
            val position = ZoomMath.ratioToPosition(ratio, 1f, 1000f)
            val back = ZoomMath.positionToRatio(position, 1f, 1000f)
            assertEquals("ratio $ratio", ratio, back, ratio * 0.005f)
        }
    }

    @Test
    fun positionIsLogarithmicSoDecadesAreEvenlySpaced() {
        val at10 = ZoomMath.ratioToPosition(10f, 1f, 1000f)
        val at100 = ZoomMath.ratioToPosition(100f, 1f, 1000f)
        assertEquals(1f / 3f, at10, 0.001f)
        assertEquals(2f / 3f, at100, 0.001f)
    }

    @Test
    fun positionsAreClampedToTheUnitInterval() {
        assertEquals(0f, ZoomMath.ratioToPosition(0.2f, 1f, 1000f), 0.0001f)
        assertEquals(1f, ZoomMath.ratioToPosition(5000f, 1f, 1000f), 0.0001f)
        assertEquals(1f, ZoomMath.positionToRatio(-3f, 1f, 1000f), 0.0001f)
        assertEquals(1000f, ZoomMath.positionToRatio(4f, 1f, 1000f), 0.01f)
    }

    @Test
    fun invalidInputsNeverEscapeTheRange() {
        assertEquals(1f, ZoomMath.sanitize(Float.NaN, 1f), 0f)
        assertEquals(1f, ZoomMath.sanitize(Float.POSITIVE_INFINITY, 1f), 0f)
        assertEquals(0f, ZoomMath.ratioToPosition(Float.NaN, 1f, 1000f), 0.0001f)
        assertEquals(1f, ZoomMath.positionToRatio(Float.NaN, 1f, 1000f), 0.0001f)
        val plan = ZoomMath.plan(Float.NaN, 1f, 10f)
        assertEquals(1f, plan.cameraRatio, 0.0001f)
        assertEquals(1f, plan.digitalFactor, 0.0001f)
    }

    @Test
    fun degenerateRangesDoNotDivideByZero() {
        val position = ZoomMath.ratioToPosition(5f, 4f, 4f)
        assertTrue("position=$position", position.isFinite() && position in 0f..1f)
        val ratio = ZoomMath.positionToRatio(0.5f, 4f, 4f)
        assertTrue("ratio=$ratio", ratio.isFinite() && ratio >= 4f)
    }

    @Test
    fun planKeepsRequestsInsideTheCameraRange() {
        val plan = ZoomMath.plan(requestedRatio = 5f, cameraMin = 1f, cameraMax = 10f)
        assertEquals(5f, plan.cameraRatio, 0.0001f)
        assertEquals(1f, plan.digitalFactor, 0.0001f)
        assertFalse(plan.isDigital)
    }

    @Test
    fun planSplitsRequestsAboveTheCameraMaximum() {
        val plan = ZoomMath.plan(requestedRatio = 1000f, cameraMin = 1f, cameraMax = 10f)
        assertEquals(10f, plan.cameraRatio, 0.0001f)
        assertEquals(100f, plan.digitalFactor, 0.0001f)
        assertTrue(plan.isDigital)
    }

    @Test
    fun planNeverSendsMoreThanTheCameraMaximumToTheHardware() {
        for (requested in listOf(1f, 9.9f, 10.1f, 250f, 1000f, 99999f)) {
            val plan = ZoomMath.plan(requested, 1f, 10f)
            assertTrue("requested=$requested", plan.cameraRatio <= 10f + 1e-4f)
            assertTrue("requested=$requested", plan.cameraRatio >= 1f - 1e-4f)
            assertTrue(plan.digitalFactor >= 1f)
        }
    }

    @Test
    fun planRespectsCamerasThatStartBelowOne() {
        val plan = ZoomMath.plan(requestedRatio = 0.4f, cameraMin = 0.5f, cameraMax = 10f)
        assertEquals(0.5f, plan.cameraRatio, 0.0001f)
    }

    @Test
    fun stopsAreReportedOncePerCrossing() {
        assertEquals(3f, ZoomMath.stopCrossed(2.5f, 3.2f))
        assertEquals(3f, ZoomMath.stopCrossed(3.2f, 2.5f))
        assertNull(ZoomMath.stopCrossed(3.2f, 3.4f))
        assertNull(ZoomMath.stopCrossed(5f, 5f))
        // Moving up through several stops reports the first one reached.
        assertEquals(10f, ZoomMath.stopCrossed(5f, 150f))
    }

    @Test
    fun ratiosAreFormattedTheWayTheOverlayShowsThem() {
        assertEquals("1×", ZoomMath.formatRatio(1f))
        assertEquals("2.5×", ZoomMath.formatRatio(2.54f))
        assertEquals("10×", ZoomMath.formatRatio(10f))
        assertEquals("100×", ZoomMath.formatRatio(100.4f))
        assertEquals("1000×", ZoomMath.formatRatio(1000f))
    }
}
