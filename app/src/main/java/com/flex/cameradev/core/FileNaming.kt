package com.flex.cameradev.core

import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale

/** Builds the file names written to MediaStore. Pure, therefore unit tested. */
object FileNaming {

    const val PHOTO_ALBUM = "A55 Super Zoom"
    const val VIDEO_ALBUM = "A55 Super Zoom"
    const val PHOTO_RELATIVE_PATH = "Pictures/$PHOTO_ALBUM"
    const val VIDEO_RELATIVE_PATH = "Movies/$VIDEO_ALBUM"

    private val TIMESTAMP: DateTimeFormatter =
        DateTimeFormatter.ofPattern("yyyyMMdd_HHmmss", Locale.US)

    fun timestamp(epochMillis: Long, zone: ZoneId = ZoneId.systemDefault()): String =
        TIMESTAMP.format(Instant.ofEpochMilli(epochMillis).atZone(zone))

    /** "A55_ZOOM_20260830_124455.jpg" */
    fun photoName(epochMillis: Long, zone: ZoneId = ZoneId.systemDefault()): String =
        "A55_ZOOM_${timestamp(epochMillis, zone)}.jpg"

    /** Uncropped companion saved next to a digitally zoomed capture. */
    fun originalPhotoName(epochMillis: Long, zone: ZoneId = ZoneId.systemDefault()): String =
        "A55_ZOOM_${timestamp(epochMillis, zone)}_ORIG.jpg"

    /**
     * "A55_VIDEO_4K30_20260830_124455.mp4" for normal clips and
     * "A55_SLOWMO_HD240_20260830_124455.mp4" for slowed down high speed clips.
     */
    fun videoName(
        mode: VideoMode,
        playback: HighSpeedPlayback,
        epochMillis: Long,
        zone: ZoneId = ZoneId.systemDefault(),
    ): String {
        val slowMotion = mode.kind == RecordingKind.HIGH_SPEED &&
            playback == HighSpeedPlayback.SLOW_MOTION
        val prefix = if (slowMotion) "A55_SLOWMO" else "A55_VIDEO"
        return "${prefix}_${mode.fileTag}_${timestamp(epochMillis, zone)}.mp4"
    }

    /** MIME types used when inserting into MediaStore. */
    const val PHOTO_MIME = "image/jpeg"
    const val VIDEO_MIME = "video/mp4"
}
