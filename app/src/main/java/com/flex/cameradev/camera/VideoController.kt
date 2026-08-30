package com.flex.cameradev.camera

import android.Manifest
import android.content.ContentValues
import android.content.Context
import android.content.pm.PackageManager
import android.net.Uri
import android.provider.MediaStore
import android.util.Log
import android.util.Range
import androidx.camera.core.DynamicRange
import androidx.camera.core.CameraInfo
import androidx.camera.video.MediaStoreOutputOptions
import androidx.camera.video.Quality
import androidx.camera.video.QualitySelector
import androidx.camera.video.Recorder
import androidx.camera.video.Recording
import androidx.camera.video.VideoCapture
import androidx.camera.video.VideoRecordEvent
import androidx.core.content.ContextCompat
import com.flex.cameradev.core.FileNaming
import com.flex.cameradev.core.HighSpeedPlayback
import com.flex.cameradev.core.SizeSpec
import com.flex.cameradev.core.VideoMode
import com.flex.cameradev.core.VideoModeId
import com.flex.cameradev.media.MediaStoreManager
import java.util.concurrent.Executor

/** Progress and completion callbacks of a normal recording. */
interface RecordingListener {
    fun onStarted()
    fun onStatus(durationMillis: Long, bytes: Long)
    fun onPaused()
    fun onResumed()
    fun onFinished(uri: Uri?, errorMessage: String?)
}

/**
 * Normal speed recording through the CameraX Recorder.
 *
 * The quality is only ever set to a value the device reported as supported, so
 * the recorder never silently substitutes a different resolution.
 */
class VideoController(
    private val context: Context,
    private val mediaStore: MediaStoreManager,
) {

    private var recording: Recording? = null
    private var activeUri: Uri? = null

    val isRecording: Boolean get() = recording != null

    /** Qualities the bound camera reports for SDR recording. */
    fun supportedQualities(cameraInfo: CameraInfo): List<Quality> = try {
        Recorder.getVideoCapabilities(cameraInfo).getSupportedQualities(DynamicRange.SDR)
    } catch (e: IllegalArgumentException) {
        Log.w(TAG, "No se pudieron leer las calidades de vídeo", e)
        emptyList()
    }

    fun buildVideoCapture(
        mode: VideoMode,
        executor: Executor,
        stabilizationEnabled: Boolean,
    ): VideoCapture<Recorder> {
        val recorder = Recorder.Builder()
            .setQualitySelector(QualitySelector.from(mode.toQuality()))
            .setExecutor(executor)
            .build()
        return VideoCapture.Builder(recorder)
            .setTargetFrameRate(Range(mode.fps, mode.fps))
            .setVideoStabilizationEnabled(stabilizationEnabled)
            .build()
    }

    /**
     * Starts a recording. Returns false when the app refuses to start, which
     * happens when storage is short or a recording is already running.
     */
    fun start(
        videoCapture: VideoCapture<Recorder>,
        mode: VideoMode,
        audioEnabled: Boolean,
        listener: RecordingListener,
    ): Boolean {
        if (recording != null) return false
        if (!mediaStore.hasRoomForRecording()) return false

        val name = FileNaming.videoName(mode, HighSpeedPlayback.REAL_TIME, System.currentTimeMillis())
        val values = ContentValues().apply {
            put(MediaStore.Video.Media.DISPLAY_NAME, name)
            put(MediaStore.Video.Media.MIME_TYPE, FileNaming.VIDEO_MIME)
            put(MediaStore.Video.Media.RELATIVE_PATH, FileNaming.VIDEO_RELATIVE_PATH)
        }
        val options = MediaStoreOutputOptions
            .Builder(
                context.contentResolver,
                MediaStore.Video.Media.getContentUri(MediaStoreManager.VOLUME_NAME),
            )
            .setContentValues(values)
            .build()

        val executor = ContextCompat.getMainExecutor(context)
        return try {
            var pending = videoCapture.output.prepareRecording(context, options)
            if (audioEnabled && hasAudioPermission()) {
                pending = pending.withAudioEnabled()
            }
            recording = pending.start(executor) { event -> dispatch(event, listener) }
            true
        } catch (e: SecurityException) {
            Log.e(TAG, "Sin permiso de micrófono para grabar", e)
            recording = null
            false
        } catch (e: IllegalStateException) {
            Log.e(TAG, "No se pudo iniciar la grabación", e)
            recording = null
            false
        }
    }

    fun pause() {
        try {
            recording?.pause()
        } catch (e: IllegalStateException) {
            Log.w(TAG, "No se pudo pausar", e)
        }
    }

    fun resume() {
        try {
            recording?.resume()
        } catch (e: IllegalStateException) {
            Log.w(TAG, "No se pudo reanudar", e)
        }
    }

    /** Stops and finalises. The listener receives the URI once the file is closed. */
    fun stop() {
        val current = recording ?: return
        try {
            current.stop()
        } catch (e: IllegalStateException) {
            Log.w(TAG, "No se pudo detener la grabación", e)
        }
    }

    /** Called when the activity goes away; guarantees the file is finalised. */
    fun finishForLifecycle() {
        stop()
        recording = null
    }

    private fun dispatch(event: VideoRecordEvent, listener: RecordingListener) {
        when (event) {
            is VideoRecordEvent.Start -> listener.onStarted()

            is VideoRecordEvent.Status -> listener.onStatus(
                event.recordingStats.recordedDurationNanos / 1_000_000L,
                event.recordingStats.numBytesRecorded,
            )

            is VideoRecordEvent.Pause -> listener.onPaused()

            is VideoRecordEvent.Resume -> listener.onResumed()

            is VideoRecordEvent.Finalize -> {
                recording = null
                activeUri = event.outputResults.outputUri.takeIf { it != Uri.EMPTY }
                val error = if (event.hasError()) describeError(event) else null
                if (error != null) {
                    activeUri?.let { mediaStore.discard(it) }
                    listener.onFinished(null, error)
                } else {
                    listener.onFinished(activeUri, null)
                }
            }
        }
    }

    private fun describeError(event: VideoRecordEvent.Finalize): String = when (event.error) {
        VideoRecordEvent.Finalize.ERROR_INSUFFICIENT_STORAGE ->
            context.getString(com.flex.cameradev.R.string.error_no_space)

        VideoRecordEvent.Finalize.ERROR_ENCODING_FAILED ->
            context.getString(com.flex.cameradev.R.string.error_encoder)

        VideoRecordEvent.Finalize.ERROR_SOURCE_INACTIVE ->
            context.getString(com.flex.cameradev.R.string.error_camera_lost)

        VideoRecordEvent.Finalize.ERROR_NO_VALID_DATA ->
            context.getString(com.flex.cameradev.R.string.error_recording_empty)

        VideoRecordEvent.Finalize.ERROR_FILE_SIZE_LIMIT_REACHED,
        VideoRecordEvent.Finalize.ERROR_DURATION_LIMIT_REACHED,
        ->
            context.getString(com.flex.cameradev.R.string.error_recording_limit)

        else -> event.cause?.message
            ?: context.getString(com.flex.cameradev.R.string.error_recording_generic)
    }

    private fun hasAudioPermission(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) ==
            PackageManager.PERMISSION_GRANTED

    private companion object {
        const val TAG = "VideoController"
    }
}

/** Maps a catalog entry to the CameraX quality bucket that matches its size. */
fun VideoMode.toQuality(): Quality = when (id) {
    VideoModeId.UHD30 -> Quality.UHD
    VideoModeId.FHD30, VideoModeId.FHD60 -> Quality.FHD
    VideoModeId.HD30, VideoModeId.HD60, VideoModeId.HD120, VideoModeId.HD240 -> Quality.HD
}

/** Nominal size of a CameraX quality bucket, used for the diagnostics list. */
fun Quality.nominalSize(): SizeSpec? = when (this) {
    Quality.UHD -> SizeSpec(3840, 2160)
    Quality.FHD -> SizeSpec(1920, 1080)
    Quality.HD -> SizeSpec(1280, 720)
    Quality.SD -> SizeSpec(720, 480)
    else -> null
}
