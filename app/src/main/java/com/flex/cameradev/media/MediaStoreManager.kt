package com.flex.cameradev.media

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.ParcelFileDescriptor
import android.os.StatFs
import android.provider.MediaStore
import android.util.Log
import com.flex.cameradev.core.FileNaming
import java.io.IOException
import java.io.OutputStream

/** A MediaStore row that is still marked pending. */
data class PendingMedia(val uri: Uri, val displayName: String)

/**
 * Owns every MediaStore interaction: rows are created pending, filled in and
 * only published once the bytes are on disk, so an interrupted capture never
 * leaves a broken entry visible in the gallery.
 */
class MediaStoreManager(private val context: Context) {

    private val resolver get() = context.contentResolver

    fun createPendingImage(displayName: String): PendingMedia? {
        val values = ContentValues().apply {
            put(MediaStore.Images.Media.DISPLAY_NAME, displayName)
            put(MediaStore.Images.Media.MIME_TYPE, FileNaming.PHOTO_MIME)
            put(MediaStore.Images.Media.RELATIVE_PATH, FileNaming.PHOTO_RELATIVE_PATH)
            put(MediaStore.Images.Media.DATE_ADDED, System.currentTimeMillis() / 1000)
            put(MediaStore.Images.Media.IS_PENDING, 1)
        }
        val collection = MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        return insert(collection, values, displayName)
    }

    fun createPendingVideo(displayName: String): PendingMedia? {
        val values = ContentValues().apply {
            put(MediaStore.Video.Media.DISPLAY_NAME, displayName)
            put(MediaStore.Video.Media.MIME_TYPE, FileNaming.VIDEO_MIME)
            put(MediaStore.Video.Media.RELATIVE_PATH, FileNaming.VIDEO_RELATIVE_PATH)
            put(MediaStore.Video.Media.DATE_ADDED, System.currentTimeMillis() / 1000)
            put(MediaStore.Video.Media.IS_PENDING, 1)
        }
        val collection = MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        return insert(collection, values, displayName)
    }

    private fun insert(collection: Uri, values: ContentValues, displayName: String): PendingMedia? =
        try {
            resolver.insert(collection, values)?.let { PendingMedia(it, displayName) }
        } catch (e: IllegalArgumentException) {
            Log.e(TAG, "MediaStore rechazó la inserción", e)
            null
        } catch (e: IllegalStateException) {
            Log.e(TAG, "MediaStore no pudo crear el archivo", e)
            null
        }

    fun openOutput(uri: Uri): OutputStream? = try {
        resolver.openOutputStream(uri, "w")
    } catch (e: IOException) {
        Log.e(TAG, "No se pudo abrir la salida", e)
        null
    } catch (e: SecurityException) {
        Log.e(TAG, "Sin permiso para escribir", e)
        null
    }

    fun openFileDescriptor(uri: Uri, mode: String = "rw"): ParcelFileDescriptor? = try {
        resolver.openFileDescriptor(uri, mode)
    } catch (e: IOException) {
        Log.e(TAG, "No se pudo abrir el descriptor", e)
        null
    } catch (e: SecurityException) {
        Log.e(TAG, "Sin permiso para el descriptor", e)
        null
    }

    /** Clears IS_PENDING so the file becomes visible to the gallery. */
    fun publish(uri: Uri): Boolean = try {
        val values = ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }
        resolver.update(uri, values, null, null) > 0
    } catch (e: IllegalArgumentException) {
        Log.e(TAG, "No se pudo publicar $uri", e)
        false
    } catch (e: SecurityException) {
        Log.e(TAG, "Sin permiso para publicar $uri", e)
        false
    }

    /** Removes a row whose bytes never made it to disk. */
    fun discard(uri: Uri) {
        try {
            resolver.delete(uri, null, null)
        } catch (e: IllegalArgumentException) {
            Log.w(TAG, "No se pudo descartar $uri", e)
        } catch (e: SecurityException) {
            Log.w(TAG, "Sin permiso para descartar $uri", e)
        }
    }

    /** True when the URI still resolves, so the gallery shortcut can be trusted. */
    fun exists(uri: Uri): Boolean = try {
        resolver.query(uri, arrayOf(MediaStore.MediaColumns._ID), null, null, null)?.use {
            it.moveToFirst()
        } ?: false
    } catch (e: IllegalArgumentException) {
        false
    } catch (e: SecurityException) {
        false
    }

    /** Free bytes on the volume the media folders live on. */
    fun freeBytes(): Long = try {
        val path = context.getExternalFilesDir(null)?.absolutePath
            ?: Environment.getDataDirectory().absolutePath
        StatFs(path).availableBytes
    } catch (e: IllegalArgumentException) {
        0L
    }

    /** Rough guard used before starting a recording. */
    fun hasRoomForRecording(minimumBytes: Long = MIN_RECORDING_BYTES): Boolean =
        freeBytes() >= minimumBytes

    companion object {
        private const val TAG = "MediaStoreManager"

        /** Around one minute of 4K footage; below this a recording is refused. */
        const val MIN_RECORDING_BYTES = 400L * 1024L * 1024L

        val VOLUME_NAME: String =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                MediaStore.VOLUME_EXTERNAL_PRIMARY
            } else {
                "external"
            }
    }
}
