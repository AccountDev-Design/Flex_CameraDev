package com.flex.cameradev.media

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.BitmapRegionDecoder
import android.graphics.Matrix
import android.graphics.Rect
import android.util.Log
import com.flex.cameradev.core.SizeSpec
import java.io.ByteArrayOutputStream
import java.io.IOException

/**
 * Produces the digitally zoomed still.
 *
 * A 50 MP JPEG is never decoded as a whole: only the region that survives the
 * crop is decoded, which keeps the peak allocation proportional to the visible
 * area instead of the sensor size.
 */
object DigitalZoomProcessor {

    private const val TAG = "DigitalZoomProcessor"

    /** Never write a file larger than this, whatever the source resolution is. */
    private const val MAX_OUTPUT_PIXELS = 24_000_000L

    /** Smallest useful edge; extreme crops are enlarged up to this. */
    private const val TARGET_MIN_EDGE = 720

    /** Enlarging beyond this only wastes space, the detail is not there. */
    private const val MAX_UPSCALE = 8f

    private const val JPEG_QUALITY = 95

    data class Result(val jpeg: ByteArray, val size: SizeSpec, val appliedUpscale: Float) {
        override fun equals(other: Any?): Boolean =
            this === other || (other is Result && size == other.size && jpeg.contentEquals(other.jpeg))

        override fun hashCode(): Int = 31 * size.hashCode() + jpeg.contentHashCode()
    }

    /** Reads the encoded size without allocating the pixels. */
    fun decodeSize(jpeg: ByteArray): SizeSpec? {
        val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(jpeg, 0, jpeg.size, options)
        if (options.outWidth <= 0 || options.outHeight <= 0) return null
        return SizeSpec(options.outWidth, options.outHeight)
    }

    /**
     * Crops the centre of [jpeg] by [factor] and rotates it by [rotationDegrees]
     * so the written pixels are already upright.
     *
     * Returns null when the source cannot be decoded; the caller then keeps the
     * untouched capture instead of losing the shot.
     */
    fun cropCenter(jpeg: ByteArray, factor: Float, rotationDegrees: Int): Result? {
        if (factor <= 1.001f || jpeg.isEmpty()) return null
        val source = decodeSize(jpeg) ?: return null

        val cropWidth = (source.width / factor).toInt().coerceIn(16, source.width)
        val cropHeight = (source.height / factor).toInt().coerceIn(16, source.height)
        val left = ((source.width - cropWidth) / 2).coerceAtLeast(0)
        val top = ((source.height - cropHeight) / 2).coerceAtLeast(0)
        val region = Rect(left, top, left + cropWidth, top + cropHeight)

        var decoder: BitmapRegionDecoder? = null
        var region0: Bitmap? = null
        var scaled: Bitmap? = null
        var rotated: Bitmap? = null
        val output = ByteArrayOutputStream(1 shl 20)
        try {
            @Suppress("DEPRECATION")
            decoder = BitmapRegionDecoder.newInstance(jpeg, 0, jpeg.size, false)

            val options = BitmapFactory.Options().apply {
                inPreferredConfig = Bitmap.Config.ARGB_8888
                inSampleSize = sampleSizeFor(cropWidth.toLong() * cropHeight.toLong())
            }
            region0 = decoder.decodeRegion(region, options) ?: return null

            val upscale = upscaleFor(region0.width, region0.height)
            scaled = if (upscale > 1.01f) {
                Bitmap.createScaledBitmap(
                    region0,
                    (region0.width * upscale).toInt().coerceAtLeast(1),
                    (region0.height * upscale).toInt().coerceAtLeast(1),
                    true,
                )
            } else {
                region0
            }

            rotated = if (rotationDegrees % 360 != 0) {
                val matrix = Matrix().apply { postRotate(rotationDegrees.toFloat()) }
                Bitmap.createBitmap(scaled, 0, 0, scaled.width, scaled.height, matrix, true)
            } else {
                scaled
            }

            rotated.compress(Bitmap.CompressFormat.JPEG, JPEG_QUALITY, output)
            return Result(
                jpeg = output.toByteArray(),
                size = SizeSpec(rotated.width, rotated.height),
                appliedUpscale = upscale,
            )
        } catch (e: IOException) {
            Log.e(TAG, "No se pudo decodificar la región", e)
            return null
        } catch (e: IllegalArgumentException) {
            Log.e(TAG, "Región de recorte inválida", e)
            return null
        } catch (e: OutOfMemoryError) {
            Log.e(TAG, "Memoria insuficiente para el recorte", e)
            return null
        } finally {
            // Bitmaps are aliased on purpose above, so only free the distinct ones.
            if (rotated !== scaled) rotated?.recycle()
            if (scaled !== region0) scaled?.recycle()
            region0?.recycle()
            decoder?.recycle()
            try {
                output.close()
            } catch (e: IOException) {
                Log.w(TAG, "No se pudo cerrar el buffer", e)
            }
        }
    }

    /** Keeps the decoded region inside the output budget. */
    internal fun sampleSizeFor(pixels: Long): Int {
        var sample = 1
        var current = pixels
        while (current > MAX_OUTPUT_PIXELS) {
            sample *= 2
            current /= 4
        }
        return sample
    }

    /** Enlargement applied to very small crops so the file stays viewable. */
    internal fun upscaleFor(width: Int, height: Int): Float {
        val shortEdge = minOf(width, height)
        if (shortEdge <= 0 || shortEdge >= TARGET_MIN_EDGE) return 1f
        val needed = TARGET_MIN_EDGE.toFloat() / shortEdge.toFloat()
        val capped = minOf(needed, MAX_UPSCALE)
        val projected = width.toLong() * height.toLong() * (capped * capped).toLong()
        return if (projected > MAX_OUTPUT_PIXELS) 1f else capped
    }
}
