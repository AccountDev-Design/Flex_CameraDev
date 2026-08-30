package com.flex.cameradev.camera

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraConstrainedHighSpeedCaptureSession
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.media.MediaRecorder
import android.net.Uri
import android.os.Handler
import android.os.HandlerThread
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.util.Log
import android.util.Range
import android.view.Surface
import androidx.core.content.ContextCompat
import com.flex.cameradev.R
import com.flex.cameradev.core.FileNaming
import com.flex.cameradev.core.HighSpeedPlayback
import com.flex.cameradev.core.VideoMode
import com.flex.cameradev.media.MediaStoreManager
import java.io.IOException
import java.util.concurrent.Executor

/**
 * Records the 120 and 240 fps modes.
 *
 * CameraX has no public high speed session, so this uses the Camera2
 * constrained high speed session directly, driving MediaRecorder. CameraX has
 * to be unbound before [start] is called; the coordinator does that.
 *
 * Audio is never recorded here: a constrained high speed session carries video
 * only, and the interface says so instead of writing a silent track.
 */
class HighSpeedVideoController(
    private val context: Context,
    private val mediaStore: MediaStoreManager,
) {

    interface Listener {
        fun onStarted()
        fun onStatus(durationMillis: Long)
        fun onFinished(uri: Uri?, errorMessage: String?)
    }

    private val cameraManager =
        context.getSystemService(Context.CAMERA_SERVICE) as? CameraManager

    private var thread: HandlerThread? = null
    private var handler: Handler? = null

    private var cameraDevice: CameraDevice? = null
    private var session: CameraConstrainedHighSpeedCaptureSession? = null
    private var recorder: MediaRecorder? = null
    private var recorderSurface: Surface? = null
    private var descriptor: ParcelFileDescriptor? = null
    private var pendingUri: Uri? = null
    private var listener: Listener? = null
    private var startedAt = 0L
    private var recorderRunning = false

    @Volatile
    var isRecording: Boolean = false
        private set

    private val ticker = object : Runnable {
        override fun run() {
            if (!isRecording) return
            listener?.onStatus(SystemClock.elapsedRealtime() - startedAt)
            handler?.postDelayed(this, STATUS_INTERVAL_MS)
        }
    }

    /**
     * Opens the camera, configures the high speed session and starts writing.
     *
     * @param previewSurface surface already resized to the mode resolution.
     */
    fun start(
        cameraId: String,
        sensorOrientation: Int,
        mode: VideoMode,
        playback: HighSpeedPlayback,
        previewSurface: Surface,
        listener: Listener,
    ): Boolean {
        if (isRecording) return false
        val manager = cameraManager ?: return fail(listener, R.string.error_camera_lost)
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            return fail(listener, R.string.error_camera_permission)
        }
        if (!mediaStore.hasRoomForRecording()) {
            return fail(listener, R.string.error_no_space)
        }
        this.listener = listener

        val name = FileNaming.videoName(mode, playback, System.currentTimeMillis())
        val pending = mediaStore.createPendingVideo(name)
            ?: return fail(listener, R.string.error_storage_write)
        pendingUri = pending.uri

        val parcel = mediaStore.openFileDescriptor(pending.uri, "rw")
        if (parcel == null) {
            cleanUpPending()
            return fail(listener, R.string.error_storage_write)
        }
        descriptor = parcel

        if (!prepareRecorder(mode, playback, sensorOrientation, parcel)) {
            releaseEverything()
            return fail(listener, R.string.error_encoder)
        }

        val backgroundThread = HandlerThread("A55-HighSpeed").apply { start() }
        thread = backgroundThread
        handler = Handler(backgroundThread.looper)

        return try {
            manager.openCamera(
                cameraId,
                object : CameraDevice.StateCallback() {
                    override fun onOpened(device: CameraDevice) {
                        cameraDevice = device
                        configureSession(device, mode, previewSurface)
                    }

                    override fun onDisconnected(device: CameraDevice) {
                        Log.w(TAG, "Cámara desconectada durante alta velocidad")
                        abort(context.getString(R.string.error_camera_lost))
                    }

                    override fun onError(device: CameraDevice, error: Int) {
                        Log.e(TAG, "Error de cámara $error en alta velocidad")
                        abort(context.getString(R.string.error_camera_lost))
                    }
                },
                handler,
            )
            true
        } catch (e: CameraAccessException) {
            Log.e(TAG, "No se pudo abrir la cámara", e)
            releaseEverything()
            fail(listener, R.string.error_camera_lost)
        } catch (e: SecurityException) {
            Log.e(TAG, "Sin permiso de cámara", e)
            releaseEverything()
            fail(listener, R.string.error_camera_permission)
        }
    }

    private fun prepareRecorder(
        mode: VideoMode,
        playback: HighSpeedPlayback,
        sensorOrientation: Int,
        parcel: ParcelFileDescriptor,
    ): Boolean {
        val playbackFps = when (playback) {
            HighSpeedPlayback.REAL_TIME -> mode.fps
            HighSpeedPlayback.SLOW_MOTION -> SLOW_MOTION_PLAYBACK_FPS
        }
        val instance = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
            MediaRecorder(context)
        } else {
            @Suppress("DEPRECATION")
            MediaRecorder()
        }
        return try {
            instance.setVideoSource(MediaRecorder.VideoSource.SURFACE)
            instance.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4)
            instance.setOutputFile(parcel.fileDescriptor)
            instance.setVideoEncoder(MediaRecorder.VideoEncoder.H264)
            instance.setVideoSize(mode.size.width, mode.size.height)
            instance.setVideoFrameRate(playbackFps)
            instance.setCaptureRate(mode.fps.toDouble())
            instance.setVideoEncodingBitRate(bitRateFor(mode))
            instance.setOrientationHint(sensorOrientation)
            instance.prepare()
            recorder = instance
            recorderSurface = instance.surface
            true
        } catch (e: IOException) {
            Log.e(TAG, "MediaRecorder no pudo prepararse", e)
            instance.release()
            false
        } catch (e: IllegalStateException) {
            Log.e(TAG, "Configuración de MediaRecorder inválida", e)
            instance.release()
            false
        } catch (e: IllegalArgumentException) {
            Log.e(TAG, "Parámetros de MediaRecorder inválidos", e)
            instance.release()
            false
        }
    }

    private fun configureSession(device: CameraDevice, mode: VideoMode, previewSurface: Surface) {
        val encoderSurface = recorderSurface ?: run {
            abort(context.getString(R.string.error_encoder))
            return
        }
        val outputs = listOf(
            OutputConfiguration(previewSurface),
            OutputConfiguration(encoderSurface),
        )
        val executor = Executor { command -> handler?.post(command) ?: command.run() }
        val configuration = SessionConfiguration(
            SessionConfiguration.SESSION_HIGH_SPEED,
            outputs,
            executor,
            object : CameraCaptureSession.StateCallback() {
                override fun onConfigured(configured: CameraCaptureSession) {
                    val highSpeed = configured as? CameraConstrainedHighSpeedCaptureSession
                    if (highSpeed == null) {
                        abort(context.getString(R.string.error_high_speed_session))
                        return
                    }
                    session = highSpeed
                    startBurst(device, highSpeed, mode, previewSurface, encoderSurface)
                }

                override fun onConfigureFailed(configured: CameraCaptureSession) {
                    Log.e(TAG, "La sesión de alta velocidad no se pudo configurar")
                    abort(context.getString(R.string.error_high_speed_session))
                }
            },
        )
        try {
            device.createCaptureSession(configuration)
        } catch (e: CameraAccessException) {
            Log.e(TAG, "No se pudo crear la sesión de alta velocidad", e)
            abort(context.getString(R.string.error_high_speed_session))
        } catch (e: IllegalArgumentException) {
            Log.e(TAG, "Configuración de alta velocidad rechazada", e)
            abort(context.getString(R.string.error_high_speed_session))
        }
    }

    private fun startBurst(
        device: CameraDevice,
        highSpeed: CameraConstrainedHighSpeedCaptureSession,
        mode: VideoMode,
        previewSurface: Surface,
        encoderSurface: Surface,
    ) {
        try {
            val builder = device.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
                addTarget(previewSurface)
                addTarget(encoderSurface)
                set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, Range(mode.fps, mode.fps))
            }
            val requests = highSpeed.createHighSpeedRequestList(builder.build())
            highSpeed.setRepeatingBurst(requests, null, handler)
            recorder?.start()
            recorderRunning = true
            isRecording = true
            startedAt = SystemClock.elapsedRealtime()
            handler?.post(ticker)
            listener?.onStarted()
        } catch (e: CameraAccessException) {
            Log.e(TAG, "No se pudo iniciar la ráfaga de alta velocidad", e)
            abort(context.getString(R.string.error_high_speed_session))
        } catch (e: IllegalStateException) {
            Log.e(TAG, "MediaRecorder rechazó el inicio", e)
            abort(context.getString(R.string.error_encoder))
        } catch (e: IllegalArgumentException) {
            Log.e(TAG, "Petición de alta velocidad inválida", e)
            abort(context.getString(R.string.error_high_speed_session))
        }
    }

    /** Stops the burst, finalises the file and publishes it. */
    fun stop() {
        if (!isRecording) {
            releaseEverything()
            return
        }
        isRecording = false
        handler?.removeCallbacks(ticker)
        var failure: String? = null
        try {
            session?.stopRepeating()
        } catch (e: CameraAccessException) {
            Log.w(TAG, "No se pudo detener la ráfaga", e)
        } catch (e: IllegalStateException) {
            Log.w(TAG, "Sesión ya cerrada", e)
        }
        try {
            if (recorderRunning) recorder?.stop()
        } catch (e: IllegalStateException) {
            // Thrown when no frame reached the encoder; the file is unusable.
            Log.e(TAG, "MediaRecorder no pudo cerrarse", e)
            failure = context.getString(R.string.error_recording_empty)
        } catch (e: RuntimeException) {
            Log.e(TAG, "MediaRecorder falló al detenerse", e)
            failure = context.getString(R.string.error_recording_generic)
        }
        recorderRunning = false

        val uri = pendingUri
        releaseEverything()
        if (failure != null || uri == null) {
            uri?.let { mediaStore.discard(it) }
            listener?.onFinished(null, failure ?: context.getString(R.string.error_recording_generic))
        } else {
            mediaStore.publish(uri)
            listener?.onFinished(uri, null)
        }
        pendingUri = null
        listener = null
    }

    /** Called when the activity is stopped; never leaves a half written file. */
    fun release() {
        if (isRecording) {
            stop()
        } else {
            releaseEverything()
        }
    }

    private fun abort(message: String) {
        isRecording = false
        recorderRunning = false
        handler?.removeCallbacks(ticker)
        pendingUri?.let { mediaStore.discard(it) }
        pendingUri = null
        releaseEverything()
        listener?.onFinished(null, message)
        listener = null
    }

    private fun cleanUpPending() {
        pendingUri?.let { mediaStore.discard(it) }
        pendingUri = null
    }

    private fun releaseEverything() {
        try {
            session?.close()
        } catch (e: IllegalStateException) {
            Log.w(TAG, "Sesión ya cerrada", e)
        }
        session = null
        cameraDevice?.close()
        cameraDevice = null
        recorder?.let {
            try {
                it.reset()
            } catch (e: IllegalStateException) {
                Log.w(TAG, "MediaRecorder ya liberado", e)
            }
            it.release()
        }
        recorder = null
        recorderSurface = null
        try {
            descriptor?.close()
        } catch (e: IOException) {
            Log.w(TAG, "No se pudo cerrar el descriptor", e)
        }
        descriptor = null
        thread?.quitSafely()
        thread = null
        handler = null
    }

    private fun fail(listener: Listener, messageRes: Int): Boolean {
        listener.onFinished(null, context.getString(messageRes))
        return false
    }

    private fun bitRateFor(mode: VideoMode): Int {
        val estimate = mode.size.area.toDouble() * mode.fps * BITS_PER_PIXEL
        return estimate.toInt().coerceIn(8_000_000, 80_000_000)
    }

    private companion object {
        const val TAG = "HighSpeedVideoController"
        const val STATUS_INTERVAL_MS = 200L

        /** Frame rate the slowed down file declares for playback. */
        const val SLOW_MOTION_PLAYBACK_FPS = 30

        const val BITS_PER_PIXEL = 0.10
    }
}
