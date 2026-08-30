package com.flex.cameradev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class HorizonCompatibilityTest {

    @Test
    fun highSpeedSessionsCannotCarryTheCorrection() {
        for (mode in listOf(VideoModeCatalog.HD120, VideoModeCatalog.HD240)) {
            val support = HorizonCompatibility.supportFor(mode, pipelineAvailable = true)
            assertFalse(mode.label, support.levelingAvailable)
            assertFalse(mode.label, support.lockAvailable)
            assertEquals(HorizonBlockReason.HIGH_SPEED_SESSION, support.blockReason)
        }
    }

    @Test
    fun withoutAPipelineNothingIsOffered() {
        val support = HorizonCompatibility.supportFor(VideoModeCatalog.FHD30, pipelineAvailable = false)
        assertFalse(support.levelingAvailable)
        assertFalse(support.lockAvailable)
        assertEquals(HorizonBlockReason.PIPELINE_UNAVAILABLE, support.blockReason)
    }

    @Test
    fun theEverydayModesGetTheFullBudget() {
        for (mode in listOf(VideoModeCatalog.FHD30, VideoModeCatalog.HD30)) {
            val support = HorizonCompatibility.supportFor(mode, pipelineAvailable = true)
            assertTrue(support.levelingAvailable)
            assertTrue(support.lockAvailable)
            assertFalse(support.experimental)
            assertEquals(HorizonCompatibility.LOCK_MAX_SCALE, support.maxScale, 0.0001f)
        }
    }

    @Test
    fun theHeavyModesAreFlaggedAsExperimentalWithASmallerBudget() {
        val uhd = HorizonCompatibility.supportFor(VideoModeCatalog.UHD30, pipelineAvailable = true)
        assertTrue(uhd.experimental)
        assertTrue(uhd.maxScale < HorizonCompatibility.LOCK_MAX_SCALE)
        assertEquals(HorizonBlockReason.PERFORMANCE_BUDGET, uhd.blockReason)

        val fhd60 = HorizonCompatibility.supportFor(VideoModeCatalog.FHD60, pipelineAvailable = true)
        assertTrue(fhd60.experimental)
        assertTrue(fhd60.lockAvailable)

        val hd60 = HorizonCompatibility.supportFor(VideoModeCatalog.HD60, pipelineAvailable = true)
        assertTrue(hd60.lockAvailable)
    }

    @Test
    fun anUnavailableModeIsDowngradedInsteadOfSilentlyKept() {
        val highSpeed = HorizonCompatibility.supportFor(VideoModeCatalog.HD240, pipelineAvailable = true)
        assertEquals(HorizonMode.OFF, highSpeed.resolve(HorizonMode.LOCK))
        assertEquals(HorizonMode.OFF, highSpeed.resolve(HorizonMode.LEVELING))

        val levelingOnly = HorizonSupport(levelingAvailable = true, lockAvailable = false, experimental = false)
        assertEquals(HorizonMode.LEVELING, levelingOnly.resolve(HorizonMode.LOCK))
        assertEquals(HorizonMode.OFF, levelingOnly.resolve(HorizonMode.OFF))
    }

    @Test
    fun levellingSpendsLessCropThanTheLock() {
        val support = HorizonCompatibility.supportFor(VideoModeCatalog.FHD30, pipelineAvailable = true)
        val leveling = HorizonCompatibility.maxScaleFor(support, HorizonMode.LEVELING)
        val lock = HorizonCompatibility.maxScaleFor(support, HorizonMode.LOCK)
        assertTrue(leveling < lock)
        assertEquals(1.0f, HorizonCompatibility.maxScaleFor(support, HorizonMode.OFF), 0.0001f)
        assertEquals(HorizonCompatibility.LEVELING_MAX_SCALE, leveling, 0.0001f)
    }

    @Test
    fun theBudgetNeverExceedsWhatTheModeAllows() {
        val uhd = HorizonCompatibility.supportFor(VideoModeCatalog.UHD30, pipelineAvailable = true)
        assertTrue(HorizonCompatibility.maxScaleFor(uhd, HorizonMode.LEVELING) <= uhd.maxScale)
        assertTrue(HorizonCompatibility.maxScaleFor(uhd, HorizonMode.LOCK) <= uhd.maxScale)
    }

    @Test
    fun photoModeOnlyLevelsTheGuide() {
        val photo = HorizonCompatibility.photoSupport()
        assertTrue(photo.levelingAvailable)
        assertFalse(photo.lockAvailable)
        assertEquals(1.0f, photo.maxScale, 0.0001f)
    }
}
