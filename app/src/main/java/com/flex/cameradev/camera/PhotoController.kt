package com.flex.cameradev.camera

import android.content.Context
import android.net.Uri
import android.util.Log
import android.util.Size
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
import androidx.camera.core.ImageProxy
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import com.flex.cameradev.core.FileNaming
import com.flex.cameradev.core.MegapixelMath
import com.flex.cameradev.core.SizeSpec
import com.flex.cameradev.media.DigitalZoomProcessor
import com.flex.cameradev.media.ExifUtils
import com.flex.cameradev.media.MediaStoreManager
import com.flex.cameradev.ui.FlashMode
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import java.io.IOException
import java.io.OutputStream
import java.util.concurrent.Executor
import kotlin.coroutines.resume

/** Outcome of a still capture. */
sealed interface PhotoResult {
    data class Saved(
        val uri: Uri,
        val savedSize: SizeSpec,
        val description: String,
        val vendorLimited: Boolean,
        val originalUri: Uri? = null,
        val digitalFactor: Float = 1f,
    ) : PhotoResult

    data class Failed(val cause: String) : PhotoResult
}

/**
 * Builds the still use case at the largest resolution the device will bind, and
 * writes the result through MediaStore.
 */
class PhotoController(
    private val context: Context,
    private val mediaStore: MediaStoreManager,
) {

    /**
     * @param targetSize resolution to ask for; the selector falls back to the
     *   closest smaller one so an unsupported request degrades instead of failing.
     */
    fun buildImageCapture(targetSize: SizeSpec?, flashMode: FlashMode): ImageCapture {
        val selectorBuilder = ResolutionSelector.Builder()
            .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
            .setAllowedResolutionMode(ResolutionSelector.PREFER_HIGHER_RESOLUTION_OVER_CAPTURE_RATE)
        if (targetSize != null && targetSize.width > 0 && targetSize.height > 0) {
            selectorBuilder.setResolutionStrategy(
                ResolutionStrategy(
                    Size(targetSize.width, targetSize.height),
                    ResolutionStrategy.FALLBACK_RULE_CLOSEST_LOWER_THEN_HIGHER,
                ),
            )
        }
        return ImageCapture.Builder()
            .setCaptureMode(ImageCapture.CAPTURE_MODE_MAXIMIZE_QUALITY)
            .setFlashMode(flashMode.toCameraX())
            .setResolutionSelector(selectorBuilder.build())
            .build()
    }

    /** Captures one frame and returns the JPEG bytes plus the rotation to apply. */
    private suspend fun captureBytes(
        imageCapture: ImageCapture,
        executor: Executor,
    ): Pair<ByteArray, Int> = suspendCancellableCoroutine { continuation ->
        imageCapture.takePicture(
            executor,
            object : ImageCapture.OnImageCapturedCallback() {
                override fun onCaptureSuccess(image: ImageProxy) {
                    try {
                        val buffer = image.planes[0].buffer
                        val bytes = ByteArray(buffer.remaining())
                        buffer.get(bytes)
                        continuation.resumeIfActive(bytes to image.imageInfo.rotationDegrees)
                    } catch (e: IllegalStateException) {
                        continuation.resumeWithFailure(e)
                    } finally {
                        image.close()
                    }
                }

                override fun onError(exception: ImageCaptureException) {
                    continuation.resumeWithFailure(exception)
                }
            },
        )
    }

    /**
     * Runs the whole still pipeline off the main thread.
     *
     * @param digitalFactor extra magnification to bake into the file; 1 keeps the
     *   original bytes untouched, which also keeps the camera's own EXIF.
     * @param advertisedSize the largest size the camera claims, used to detect a
     *   vendor imposed limit.
     */
    suspend fun capture(
        imageCapture: ImageCapture,
        executor: Executor,
        digitalFactor: Float,
        advertisedSize: SizeSpec?,
        saveOriginalToo: Boolean,
    ): PhotoResult = try {
        val (bytes, rotation) = captureBytes(imageCapture, executor)
        withContext(Dispatchers.IO) { persist(bytes, rotation, digitalFactor, advertisedSize, saveOriginalToo) }
    } catch (e: ImageCaptureException) {
        Log.e(TAG, "La captura falló", e)
        PhotoResult.Failed(e.message ?: "ImageCaptureException")
    } catch (e: IllegalStateException) {
        Log.e(TAG, "Estado inválido durante la captura", e)
        PhotoResult.Failed(e.message ?: "IllegalStateException")
    }

    private fun persist(
        bytes: ByteArray,
        rotationDegrees: Int,
        digitalFactor: Float,
        advertisedSize: SizeSpec?,
        saveOriginalToo: Boolean,
    ): PhotoResult {
        val timestamp = System.currentTimeMillis()
        val cropped = if (digitalFactor > 1.001f) {
            DigitalZoomProcessor.cropCenter(bytes, digitalFactor, rotationDegrees)
        } else {
            null
        }

        val originalUri = if (cropped != null && saveOriginalToo) {
            writeJpeg(bytes, FileNaming.originalPhotoName(timestamp), null)
        } else {
            null
        }

        val payload = cropped?.jpeg ?: bytes
        val name = FileNaming.photoName(timestamp)
        val uri = writeJpeg(payload, name, if (cropped != null) bytes else null, digitalFactor)
            ?: return PhotoResult.Failed(context.getString(com.flex.cameradev.R.string.error_storage_write))

        val encodedSize = cropped?.size
            ?: DigitalZoomProcessor.decodeSize(bytes)
            ?: SizeSpec(0, 0)
        val displayed = if (cropped != null) {
            encodedSize
        } else {
            ExifUtils.displayedSize(encodedSize, rotationDegrees)
        }

        val vendorLimited = advertisedSize != null &&
            cropped == null &&
            MegapixelMath.isVendorLimited(displayed, advertisedSize)

        return PhotoResult.Saved(
            uri = uri,
            savedSize = displayed,
            description = MegapixelMath.describe(displayed.width, displayed.height),
            vendorLimited = vendorLimited,
            originalUri = originalUri,
            digitalFactor = digitalFactor,
        )
    }

    /**
     * Writes one JPEG through MediaStore. [exifSource] is the untouched capture
     * whose tags are copied when the payload was re-encoded.
     */
    private fun writeJpeg(
        payload: ByteArray,
        displayName: String,
        exifSource: ByteArray?,
        digitalFactor: Float = 1f,
    ): Uri? {
        val pending = mediaStore.createPendingImage(displayName) ?: return null
        var stream: OutputStream? = null
        return try {
            stream = mediaStore.openOutput(pending.uri) ?: run {
                mediaStore.discard(pending.uri)
                return null
            }
            stream.write(payload)
            stream.flush()
            stream.close()
            stream = null
            if (exifSource != null) {
                writeExif(pending.uri, exifSource, digitalFactor)
            }
            mediaStore.publish(pending.uri)
            pending.uri
        } catch (e: IOException) {
            Log.e(TAG, "No se pudo escribir $displayName", e)
            mediaStore.discard(pending.uri)
            null
        } finally {
            try {
                stream?.close()
            } catch (e: IOException) {
                Log.w(TAG, "No se pudo cerrar el flujo", e)
            }
        }
    }

    /** Re-opens the pending file to copy the EXIF tags onto the re-encoded bytes. */
    private fun writeExif(uri: Uri, source: ByteArray, digitalFactor: Float) {
        val descriptor = mediaStore.openFileDescriptor(uri, "rw") ?: return
        try {
            descriptor.use { parcel ->
                ExifUtils.transferTags(source, parcel.fileDescriptor, digitalFactor)
            }
        } catch (e: IOException) {
            Log.w(TAG, "No se pudo cerrar el descriptor EXIF", e)
        }
    }

    private fun <T> CancellableContinuation<T>.resumeIfActive(value: T) {
        if (isActive) resume(value)
    }

    private fun <T> CancellableContinuation<T>.resumeWithFailure(cause: Throwable) {
        if (isActive) resumeWith(Result.failure(cause))
    }

    private companion object {
        const val TAG = "PhotoController"
    }
}

fun FlashMode.toCameraX(): Int = when (this) {
    FlashMode.AUTO -> ImageCapture.FLASH_MODE_AUTO
    FlashMode.ON -> ImageCapture.FLASH_MODE_ON
    FlashMode.OFF -> ImageCapture.FLASH_MODE_OFF
}
