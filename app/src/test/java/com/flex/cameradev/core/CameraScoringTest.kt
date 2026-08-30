package com.flex.cameradev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CameraScoringTest {

    /** A rear camera set shaped like the one a Galaxy A55 class device exposes. */
    private val mainFiftyMp = CameraCandidate(
        id = "0",
        facingBack = true,
        maxJpegSize = SizeSpec(8160, 6120),
        hasOpticalStabilization = true,
        sensorWidthMm = 6.4f,
        focalLengthMm = 5.4f,
        minimumFocusDistanceDiopters = 10f,
        isLogical = true,
        physicalIds = setOf("0", "4"),
        maxVideoSize = SizeSpec(3840, 2160),
    )
    private val ultraWideTwelveMp = CameraCandidate(
        id = "2",
        facingBack = true,
        maxJpegSize = SizeSpec(4000, 3000),
        hasOpticalStabilization = false,
        sensorWidthMm = 3.6f,
        focalLengthMm = 1.9f,
        minimumFocusDistanceDiopters = 0f,
        isLogical = false,
        maxVideoSize = SizeSpec(1920, 1080),
    )
    private val macroFiveMp = CameraCandidate(
        id = "3",
        facingBack = true,
        maxJpegSize = SizeSpec(2576, 1932),
        hasOpticalStabilization = false,
        sensorWidthMm = 3.0f,
        focalLengthMm = 2.4f,
        minimumFocusDistanceDiopters = 0f,
        isLogical = false,
        supportsVideoRecording = false,
    )
    private val frontCamera = CameraCandidate(
        id = "1",
        facingBack = false,
        maxJpegSize = SizeSpec(8160, 6120),
        hasOpticalStabilization = false,
        sensorWidthMm = 5.0f,
        focalLengthMm = 3.8f,
        minimumFocusDistanceDiopters = 0f,
        isLogical = false,
    )

    private val all = listOf(ultraWideTwelveMp, macroFiveMp, mainFiftyMp, frontCamera)

    @Test
    fun theHighResolutionRearSensorIsChosen() {
        assertEquals(mainFiftyMp, CameraScoring.pickMain(all))
    }

    @Test
    fun theChoiceDoesNotDependOnTheIdBeingZero() {
        val relabelled = all.map { if (it.id == "0") it.copy(id = "7") else it }
        assertEquals("7", CameraScoring.pickMain(relabelled)?.id)
    }

    @Test
    fun frontCamerasAreNeverChosenAsTheMain() {
        assertNull(CameraScoring.pickMain(listOf(frontCamera)))
        assertTrue(CameraScoring.rank(all).none { !it.facingBack })
    }

    @Test
    fun anEmptyDeviceReturnsNothingInsteadOfGuessing() {
        assertNull(CameraScoring.pickMain(emptyList()))
    }

    @Test
    fun theUltraWideModuleIsRecognised() {
        assertTrue(CameraScoring.isUltraWide(ultraWideTwelveMp))
        assertFalse(CameraScoring.isUltraWide(mainFiftyMp))
        assertTrue(ultraWideTwelveMp.horizontalFovDegrees > mainFiftyMp.horizontalFovDegrees)
    }

    @Test
    fun theMacroModuleIsRecognised() {
        assertTrue(CameraScoring.isHelperModule(macroFiveMp))
        assertFalse(CameraScoring.isHelperModule(mainFiftyMp))
        assertFalse(CameraScoring.isHelperModule(ultraWideTwelveMp))
    }

    @Test
    fun withinTheSameResolutionTierStabilisationDecides() {
        val stabilised = ultraWideTwelveMp.copy(
            id = "9",
            hasOpticalStabilization = true,
            sensorWidthMm = 6.0f,
            focalLengthMm = 5.0f,
            minimumFocusDistanceDiopters = 10f,
        )
        val chosen = CameraScoring.pickMain(listOf(ultraWideTwelveMp, stabilised))
        assertEquals("9", chosen?.id)
    }

    @Test
    fun aTiedPairFallsBackToAStableOrderInsteadOfRandomness() {
        val a = mainFiftyMp.copy(id = "b")
        val b = mainFiftyMp.copy(id = "a")
        assertEquals("a", CameraScoring.pickMain(listOf(a, b))?.id)
        assertEquals("a", CameraScoring.pickMain(listOf(b, a))?.id)
    }

    @Test
    fun camerasWithoutOpticalDataAreStillRanked() {
        val unknown = CameraCandidate(
            id = "5",
            facingBack = true,
            maxJpegSize = SizeSpec(4000, 3000),
            hasOpticalStabilization = false,
            sensorWidthMm = 0f,
            focalLengthMm = 0f,
            minimumFocusDistanceDiopters = 0f,
            isLogical = false,
        )
        assertEquals(0f, unknown.horizontalFovDegrees, 0.0001f)
        assertEquals("5", CameraScoring.pickMain(listOf(unknown))?.id)
    }

    @Test
    fun theRankingIsCompleteAndOrdered() {
        val ranked = CameraScoring.rank(all)
        assertEquals(3, ranked.size)
        assertEquals(mainFiftyMp, ranked.first())
        assertEquals(macroFiveMp, ranked.last())
    }
}
