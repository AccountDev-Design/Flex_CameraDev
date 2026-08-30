package com.flex.cameradev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ResolutionSelectionTest {

    private val standard = listOf(
        SizeSpec(4080, 3060),
        SizeSpec(4000, 3000),
        SizeSpec(1920, 1080),
        SizeSpec(640, 480),
    )
    private val highRes = listOf(SizeSpec(8160, 6120))

    @Test
    fun theHighResolutionListWinsOverTheStandardOne() {
        assertEquals(SizeSpec(8160, 6120), ResolutionSelection.bestJpeg(standard, highRes))
    }

    @Test
    fun withoutHighResolutionSizesTheLargestStandardOneIsUsed() {
        assertEquals(SizeSpec(4080, 3060), ResolutionSelection.bestJpeg(standard))
    }

    @Test
    fun candidatesAreOrderedLargestFirstAndDeduplicated() {
        val ordered = ResolutionSelection.orderedJpegCandidates(
            standard + SizeSpec(8160, 6120),
            highRes,
        )
        assertEquals(ordered.toSet().size, ordered.size)
        for (i in 1 until ordered.size) {
            assertTrue(ordered[i - 1].area >= ordered[i].area)
        }
    }

    @Test
    fun equalAreasPreferTheNativeFourThreeRatio() {
        val ordered = ResolutionSelection.orderedJpegCandidates(
            listOf(SizeSpec(4800, 2500), SizeSpec(4000, 3000)),
        )
        assertEquals(SizeSpec(4000, 3000), ordered.first())
    }

    @Test
    fun invalidSizesAreDropped() {
        val ordered = ResolutionSelection.orderedJpegCandidates(
            listOf(SizeSpec(0, 0), SizeSpec(-1, 100), SizeSpec(640, 480)),
        )
        assertEquals(listOf(SizeSpec(640, 480)), ordered)
    }

    @Test
    fun theFallbackWalksDownOneStepAtATime() {
        val ordered = ResolutionSelection.orderedJpegCandidates(standard, highRes)
        var current = ResolutionSelection.nextSmaller(ordered, null)
        assertEquals(SizeSpec(8160, 6120), current)
        current = ResolutionSelection.nextSmaller(ordered, current)
        assertEquals(SizeSpec(4080, 3060), current)
        current = ResolutionSelection.nextSmaller(ordered, current)
        assertEquals(SizeSpec(4000, 3000), current)
    }

    @Test
    fun theFallbackEndsInsteadOfLooping() {
        val ordered = listOf(SizeSpec(1920, 1080))
        assertNull(ResolutionSelection.nextSmaller(ordered, SizeSpec(1920, 1080)))
        assertNull(ResolutionSelection.nextSmaller(emptyList(), null))
    }

    @Test
    fun anUnknownCurrentSizeStillDegrades() {
        val ordered = ResolutionSelection.orderedJpegCandidates(standard, highRes)
        assertEquals(SizeSpec(1920, 1080), ResolutionSelection.nextSmaller(ordered, SizeSpec(3000, 2000)))
    }

    @Test
    fun thePreviewMatchesTheCaptureRatioWithinItsBudget() {
        val previews = listOf(
            SizeSpec(1920, 1080),
            SizeSpec(1440, 1080),
            SizeSpec(1280, 720),
            SizeSpec(3840, 2160),
        )
        val chosen = ResolutionSelection.previewSizeFor(SizeSpec(8160, 6120), previews)
        assertEquals(SizeSpec(1440, 1080), chosen)
    }

    @Test
    fun thePreviewNeverExceedsItsAreaBudget() {
        val previews = listOf(SizeSpec(3840, 2160), SizeSpec(1280, 720))
        val chosen = ResolutionSelection.previewSizeFor(SizeSpec(1920, 1080), previews)
        assertEquals(SizeSpec(1280, 720), chosen)
    }

    @Test
    fun anEmptyPreviewListReturnsNull() {
        assertNull(ResolutionSelection.previewSizeFor(SizeSpec(1920, 1080), emptyList()))
    }

    @Test
    fun aFixedFrameRateRangeIsPreferred() {
        val ranges = listOf(7..30, 30..30, 24..60, 60..60)
        assertEquals(60..60, ResolutionSelection.pickFpsRange(60, ranges))
        assertEquals(30..30, ResolutionSelection.pickFpsRange(30, ranges))
    }

    @Test
    fun aVariableRangeIsAcceptedWhenNoFixedOneExists() {
        val ranges = listOf(7..30, 15..60)
        assertEquals(15..60, ResolutionSelection.pickFpsRange(60, ranges))
        assertFalse(ResolutionSelection.supportsFixedFps(60, ranges))
        assertTrue(ResolutionSelection.supportsFixedFps(60, listOf(60..60)))
    }

    @Test
    fun anUnsupportedFrameRateReturnsNull() {
        assertNull(ResolutionSelection.pickFpsRange(120, listOf(7..30, 30..30)))
        assertNull(ResolutionSelection.pickFpsRange(0, listOf(30..30)))
        assertNull(ResolutionSelection.pickFpsRange(30, emptyList()))
    }
}
