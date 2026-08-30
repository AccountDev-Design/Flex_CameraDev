package com.flex.cameradev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class VideoModeTest {

    private fun availability(vararg available: VideoModeId): List<VideoModeAvailability> =
        VideoModeCatalog.ALL.map { mode ->
            VideoModeAvailability(
                mode = mode,
                available = mode.id in available,
                reason = if (mode.id in available) null else UnavailableReason.CAMERA_FPS_UNSUPPORTED,
            )
        }

    @Test
    fun theCatalogCoversExactlyTheRequestedCombinations() {
        assertEquals(7, VideoModeCatalog.ALL.size)
        assertEquals(
            listOf("4K · 30", "FHD · 30", "FHD · 60", "HD · 30", "HD · 60", "HD · 120", "HD · 240"),
            VideoModeCatalog.ALL.map { it.label },
        )
    }

    @Test
    fun onlyTheTwoFastModesUseAHighSpeedSession() {
        val highSpeed = VideoModeCatalog.ALL.filter { it.kind == RecordingKind.HIGH_SPEED }
        assertEquals(listOf(VideoModeId.HD120, VideoModeId.HD240), highSpeed.map { it.id })
        assertTrue(highSpeed.all { it.fps >= 120 })
    }

    @Test
    fun noFourKModeAboveThirtyFramesIsAdvertised() {
        val fourK = VideoModeCatalog.ALL.filter { it.size == SizeSpec(3840, 2160) }
        assertEquals(1, fourK.size)
        assertEquals(30, fourK.single().fps)
    }

    @Test
    fun anAvailablePreferenceIsKept() {
        val list = availability(VideoModeId.UHD30, VideoModeId.FHD30)
        assertEquals(VideoModeId.UHD30, VideoModeCatalog.fallbackFor(VideoModeId.UHD30, list)?.mode?.id)
    }

    @Test
    fun anUnavailablePreferenceFallsBackToASafeMode() {
        val list = availability(VideoModeId.FHD30, VideoModeId.HD30)
        assertEquals(VideoModeId.FHD30, VideoModeCatalog.fallbackFor(VideoModeId.UHD30, list)?.mode?.id)
    }

    @Test
    fun theFallbackNeverPicksAHighSpeedMode() {
        val list = availability(VideoModeId.HD240)
        assertNull(VideoModeCatalog.fallbackFor(VideoModeId.UHD30, list))
    }

    @Test
    fun aDeviceWithNothingAvailableReturnsNull() {
        assertNull(VideoModeCatalog.fallbackFor(VideoModeId.FHD30, availability()))
    }

    @Test
    fun availabilityCarriesTheReasonWhenBlocked() {
        val blocked = availability(VideoModeId.FHD30).first { it.mode.id == VideoModeId.HD240 }
        assertFalse(blocked.available)
        assertNotNull(blocked.reason)
        assertTrue(blocked.isHighSpeed)
    }

    @Test
    fun everyModeCanBeLookedUpById() {
        for (id in VideoModeId.entries) {
            assertEquals(id, VideoModeCatalog.byId(id).id)
        }
    }
}
