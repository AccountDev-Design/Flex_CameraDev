package com.flex.cameradev.media

import android.util.Log
import androidx.exifinterface.media.ExifInterface
import com.flex.cameradev.core.SizeSpec
import java.io.ByteArrayInputStream
import java.io.FileDescriptor
import java.io.IOException

/**
 * EXIF handling for the capture pipeline.
 *
 * Re-encoding a cropped bitmap drops every tag, so the relevant ones are copied
 * over and the orientation is rewritten to match the pixels that were written.
 */
object ExifUtils {

    private const val TAG = "ExifUtils"

    private const val SOFTWARE_TAG = "A55 Super Zoom"

    /** Tags worth preserving when a capture is re-encoded after cropping. */
    private val COPIED_TAGS = arrayOf(
        ExifInterface.TAG_DATETIME,
        ExifInterface.TAG_DATETIME_ORIGINAL,
        ExifInterface.TAG_DATETIME_DIGITIZED,
        ExifInterface.TAG_SUBSEC_TIME,
        ExifInterface.TAG_OFFSET_TIME,
        ExifInterface.TAG_MAKE,
        ExifInterface.TAG_MODEL,
        ExifInterface.TAG_F_NUMBER,
        ExifInterface.TAG_EXPOSURE_TIME,
        ExifInterface.TAG_PHOTOGRAPHIC_SENSITIVITY,
        ExifInterface.TAG_FOCAL_LENGTH,
        ExifInterface.TAG_FOCAL_LENGTH_IN_35MM_FILM,
        ExifInterface.TAG_WHITE_BALANCE,
        ExifInterface.TAG_FLASH,
        ExifInterface.TAG_METERING_MODE,
        ExifInterface.TAG_EXPOSURE_BIAS_VALUE,
        ExifInterface.TAG_COLOR_SPACE,
    )

    /** Size the viewer will see, i.e. after the EXIF rotation is applied. */
    fun displayedSize(size: SizeSpec, rotationDegrees: Int): SizeSpec =
        if (rotationDegrees == 90 || rotationDegrees == 270) {
            SizeSpec(size.height, size.width)
        } else {
            size
        }

    /**
     * Copies the descriptive tags from [sourceJpeg] into the file behind
     * [descriptor] and stores an orientation of "already upright", which is
     * correct because the crop pipeline bakes the rotation into the pixels.
     */
    fun transferTags(sourceJpeg: ByteArray, descriptor: FileDescriptor, digitalFactor: Float = 1f) {
        val source = try {
            ExifInterface(ByteArrayInputStream(sourceJpeg))
        } catch (e: IOException) {
            Log.w(TAG, "El origen no tiene EXIF legible", e)
            null
        }
        try {
            val target = ExifInterface(descriptor)
            source?.let { origin ->
                for (tag in COPIED_TAGS) {
                    origin.getAttribute(tag)?.let { target.setAttribute(tag, it) }
                }
            }
            target.setAttribute(
                ExifInterface.TAG_ORIENTATION,
                ExifInterface.ORIENTATION_NORMAL.toString(),
            )
            target.setAttribute(ExifInterface.TAG_SOFTWARE, SOFTWARE_TAG)
            if (digitalFactor > 1.001f) {
                target.setAttribute(
                    ExifInterface.TAG_USER_COMMENT,
                    describeDigitalZoom(digitalFactor),
                )
            }
            target.saveAttributes()
        } catch (e: IOException) {
            Log.w(TAG, "No se pudieron escribir las etiquetas EXIF", e)
        }
    }

    /** Records the applied digital magnification so the file explains itself. */
    fun describeDigitalZoom(factor: Float): String =
        "Recorte digital ×" + String.format(java.util.Locale.US, "%.1f", factor)
}
