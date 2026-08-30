package com.flex.cameradev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

class CropMathTest {

    @Test
    fun aLevelFrameIsNeverScaled() {
        assertEquals(1f, CropMath.coverScale(0f, 1920, 1080), 0.0001f)
        assertEquals(0f, CropMath.croppedAreaPercent(1f), 0.0001f)
    }

    @Test
    fun theScaleMatchesTheAnalyticValue() {
        // Rotating a 16:9 frame by 90 degrees needs 16/9 magnification to cover it.
        assertEquals(16f / 9f, CropMath.coverScale(90f, 1920, 1080), 0.001f)
        // 10 degrees on 16:9 has to satisfy both the width and the height constraint.
        val byWidth = (1920.0 * 0.984807753 + 1080.0 * 0.173648178) / 1920.0
        val byHeight = (1920.0 * 0.173648178 + 1080.0 * 0.984807753) / 1080.0
        assertEquals(maxOf(byWidth, byHeight).toFloat(), CropMath.coverScale(10f, 1920, 1080), 0.001f)
    }

    @Test
    fun theScaleIsSymmetricAndMonotonic() {
        assertEquals(
            CropMath.coverScale(12f, 1920, 1080),
            CropMath.coverScale(-12f, 1920, 1080),
            0.0001f,
        )
        var previous = 1f
        for (angle in 0..45) {
            val scale = CropMath.coverScale(angle.toFloat(), 1920, 1080)
            assertTrue("angle=$angle", scale >= previous - 1e-4f)
            previous = scale
        }
    }

    @Test
    fun degenerateSizesFallBackToNoScaling() {
        assertEquals(1f, CropMath.coverScale(20f, 0, 1080), 0.0001f)
        assertEquals(1f, CropMath.coverScale(20f, 1920, 0), 0.0001f)
    }

    @Test
    fun croppedAreaIsReportedAsAPercentage() {
        assertEquals(0f, CropMath.croppedAreaPercent(1f), 0.001f)
        assertEquals(75f, CropMath.croppedAreaPercent(2f), 0.001f)
        assertEquals(0f, CropMath.croppedAreaPercent(0.5f), 0.001f)
        assertEquals(0f, CropMath.croppedAreaPercent(Float.NaN), 0.001f)
    }

    @Test
    fun theCorrectableRollGrowsWithTheCropBudget() {
        val small = CropMath.maxCorrectableRoll(1.18f, 1920, 1080)
        val large = CropMath.maxCorrectableRoll(1.60f, 1920, 1080)
        assertTrue("$small should be smaller than $large", small < large)
        assertEquals(0f, CropMath.maxCorrectableRoll(1.0f, 1920, 1080), 0.0001f)
        assertEquals(90f, CropMath.maxCorrectableRoll(4f, 1920, 1080), 0.0001f)
    }

    @Test
    fun theCorrectableRollStaysInsideItsBudget() {
        for (budget in listOf(1.05f, 1.18f, 1.35f, 1.6f)) {
            val limit = CropMath.maxCorrectableRoll(budget, 1920, 1080)
            assertTrue(CropMath.coverScale(limit, 1920, 1080) <= budget + 1e-3f)
        }
    }

    @Test
    fun smallTiltsAreCorrectedOneToOne() {
        assertEquals(3f, CropMath.softLimit(3f, 20f), 0.0001f)
        assertEquals(-3f, CropMath.softLimit(-3f, 20f), 0.0001f)
    }

    @Test
    fun largeTiltsEaseIntoTheLimitInsteadOfSnapping() {
        val limit = 20f
        assertTrue(CropMath.softLimit(60f, limit) < limit)
        assertTrue(CropMath.softLimit(60f, limit) > limit * 0.9f)
        // The function stays continuous around the knee.
        val below = CropMath.softLimit(13.99f, limit)
        val above = CropMath.softLimit(14.01f, limit)
        assertTrue(abs(below - above) < 0.05f)
        // And monotonic.
        var previous = 0f
        for (i in 0..900) {
            val value = CropMath.softLimit(i / 10f, limit)
            assertTrue(value >= previous - 1e-4f)
            assertTrue(value <= limit + 1e-4f)
            previous = value
        }
    }

    @Test
    fun aZeroBudgetDisablesTheCorrection() {
        assertEquals(0f, CropMath.softLimit(45f, 0f), 0.0001f)
    }

    @Test
    fun thePlanReportsTheLimitStateAndNeverExceedsTheBudget() {
        val relaxed = CropMath.plan(measuredRoll = 4f, width = 1920, height = 1080, maxScale = 1.6f)
        assertFalse(relaxed.atLimit)
        assertEquals(4f, relaxed.appliedRoll, 0.0001f)
        assertTrue(relaxed.scale <= 1.6f)

        val clipped = CropMath.plan(measuredRoll = 80f, width = 1920, height = 1080, maxScale = 1.18f)
        assertTrue(clipped.atLimit)
        assertTrue(abs(clipped.appliedRoll) < 80f)
        assertTrue(clipped.scale <= 1.18f + 1e-3f)
        assertTrue(clipped.croppedPercent > 0f)
        assertEquals(80f - clipped.appliedRoll, clipped.residualRoll, 0.0001f)
    }

    @Test
    fun thePlanNeverProducesInvalidNumbers() {
        val plan = CropMath.plan(Float.NaN, 1920, 1080, Float.NaN)
        assertTrue(plan.appliedRoll.isFinite())
        assertTrue(plan.scale.isFinite())
        assertTrue(plan.scale >= 1f)
    }
}
