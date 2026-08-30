package com.flex.cameradev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.ZoneId

class FileNamingTest {

    // 2026-08-30T12:44:55Z
    private val instant = 1788093895000L
    private val utc: ZoneId = ZoneId.of("UTC")

    @Test
    fun photoNamesFollowTheDocumentedPattern() {
        assertEquals("A55_ZOOM_20260830_124455.jpg", FileNaming.photoName(instant, utc))
    }

    @Test
    fun theUncroppedCompanionIsMarked() {
        assertEquals("A55_ZOOM_20260830_124455_ORIG.jpg", FileNaming.originalPhotoName(instant, utc))
    }

    @Test
    fun normalVideoNamesCarryTheModeTag() {
        assertEquals(
            "A55_VIDEO_4K30_20260830_124455.mp4",
            FileNaming.videoName(VideoModeCatalog.UHD30, HighSpeedPlayback.REAL_TIME, instant, utc),
        )
        assertEquals(
            "A55_VIDEO_FHD60_20260830_124455.mp4",
            FileNaming.videoName(VideoModeCatalog.FHD60, HighSpeedPlayback.REAL_TIME, instant, utc),
        )
    }

    @Test
    fun slowMotionClipsUseTheSlowMoPrefix() {
        assertEquals(
            "A55_SLOWMO_HD240_20260830_124455.mp4",
            FileNaming.videoName(VideoModeCatalog.HD240, HighSpeedPlayback.SLOW_MOTION, instant, utc),
        )
    }

    @Test
    fun aHighSpeedClipKeptAtRealTimeIsNotCalledSlowMotion() {
        assertEquals(
            "A55_VIDEO_HD240_20260830_124455.mp4",
            FileNaming.videoName(VideoModeCatalog.HD240, HighSpeedPlayback.REAL_TIME, instant, utc),
        )
    }

    @Test
    fun aNormalModeIsNeverRenamedToSlowMotion() {
        assertEquals(
            "A55_VIDEO_FHD30_20260830_124455.mp4",
            FileNaming.videoName(VideoModeCatalog.FHD30, HighSpeedPlayback.SLOW_MOTION, instant, utc),
        )
    }

    @Test
    fun everyCatalogEntryProducesAUniqueName() {
        val names = VideoModeCatalog.ALL.map {
            FileNaming.videoName(it, HighSpeedPlayback.REAL_TIME, instant, utc)
        }
        assertEquals(names.size, names.toSet().size)
        assertTrue(names.all { it.endsWith(".mp4") })
    }

    @Test
    fun albumPathsMatchTheDocumentedFolders() {
        assertEquals("Pictures/A55 Super Zoom", FileNaming.PHOTO_RELATIVE_PATH)
        assertEquals("Movies/A55 Super Zoom", FileNaming.VIDEO_RELATIVE_PATH)
    }

    @Test
    fun namesContainNoCharactersMediaStoreRejects() {
        val illegal = charArrayOf('/', '\\', ':', '*', '?', '"', '<', '>', '|')
        val candidates = listOf(
            FileNaming.photoName(instant, utc),
            FileNaming.originalPhotoName(instant, utc),
        ) + VideoModeCatalog.ALL.map {
            FileNaming.videoName(it, HighSpeedPlayback.SLOW_MOTION, instant, utc)
        }
        for (name in candidates) {
            assertTrue(name, illegal.none { name.contains(it) })
        }
    }
}
