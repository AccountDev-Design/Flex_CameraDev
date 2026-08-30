package com.flex.cameradev.ui

import android.Manifest
import android.app.AlertDialog
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import android.util.Size
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.flex.cameradev.R
import com.flex.cameradev.camera.CameraCoordinator
import com.flex.cameradev.camera.FocusController
import com.flex.cameradev.camera.HighSpeedVideoController
import com.flex.cameradev.camera.PhotoController
import com.flex.cameradev.camera.PhotoResult
import com.flex.cameradev.camera.RecordingListener
import com.flex.cameradev.camera.VideoController
import com.flex.cameradev.camera.ZoomController
import com.flex.cameradev.core.HighSpeedPlayback
import com.flex.cameradev.core.HorizonCorrection
import com.flex.cameradev.core.HorizonMode
import com.flex.cameradev.core.MegapixelMath
import com.flex.cameradev.core.RecordingKind
import com.flex.cameradev.core.VideoModeAvailability
import com.flex.cameradev.core.ZoomMath
import com.flex.cameradev.horizon.HorizonController
import com.flex.cameradev.media.MediaStoreManager
import com.flex.cameradev.ui.views.FocusRingView
import com.flex.cameradev.ui.views.GridOverlayView
import com.flex.cameradev.ui.views.HorizonOverlayView
import com.flex.cameradev.ui.views.LiquidGlassPanel
import com.flex.cameradev.ui.views.ShutterButton
import com.flex.cameradev.ui.views.ZoomSlider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.IOException
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * The only screen of the app.
 *
 * It renders [CameraUiState] and forwards user intent to the coordinator; it
 * never touches CameraX directly, which is what keeps the controls and the
 * camera configuration from drifting apart.
 */
class MainActivity : AppCompatActivity() {

    private val viewModel: CameraViewModel by viewModels()

    private lateinit var previewContainer: ViewGroup
    private lateinit var previewView: PreviewView
    private lateinit var highSpeedPreview: SurfaceView
    private lateinit var gridOverlay: GridOverlayView
    private lateinit var horizonOverlay: HorizonOverlayView
    private lateinit var focusRing: FocusRingView
    private lateinit var flashOverlay: View

    private lateinit var topPanel: LiquidGlassPanel
    private lateinit var bottomPanel: LiquidGlassPanel
    private lateinit var qualityPanel: View
    private lateinit var horizonPanel: View

    private lateinit var modeSummary: TextView
    private lateinit var resolutionHint: TextView
    private lateinit var flashButton: ImageButton
    private lateinit var micButton: ImageButton
    private lateinit var stabilizerButton: ImageButton
    private lateinit var gridButton: ImageButton
    private lateinit var infoButton: ImageButton

    private lateinit var recordingChip: View
    private lateinit var recDot: View
    private lateinit var recTimer: TextView
    private lateinit var recSize: TextView
    private lateinit var pauseButton: TextView
    private lateinit var processingIndicator: ProgressBar
    private lateinit var noticeBanner: TextView

    private lateinit var zoomValue: TextView
    private lateinit var zoomSource: TextView
    private lateinit var zoomLimit: TextView
    private lateinit var zoomSlider: ZoomSlider
    private lateinit var quickZoomButtons: List<Pair<TextView, Float>>

    private lateinit var qualityButton: TextView
    private lateinit var horizonButton: TextView
    private lateinit var horizonStatus: TextView
    private lateinit var photoTab: TextView
    private lateinit var videoTab: TextView
    private lateinit var thumbnailButton: ImageButton
    private lateinit var shutterButton: ShutterButton
    private lateinit var saveOriginalToggle: TextView

    private lateinit var qualityModeContainer: LinearLayout
    private lateinit var qualitySummaryResolution: TextView
    private lateinit var qualitySummaryFps: TextView
    private lateinit var qualitySummaryType: TextView
    private lateinit var qualitySummaryAudio: TextView
    private lateinit var qualitySummaryStabilization: TextView
    private lateinit var qualitySummaryHorizon: TextView
    private lateinit var qualityFreeSpace: TextView
    private lateinit var playbackTitle: TextView
    private lateinit var playbackRow: View
    private lateinit var playbackRealTime: TextView
    private lateinit var playbackSlowMotion: TextView

    private lateinit var horizonOffChip: TextView
    private lateinit var horizonLevelChip: TextView
    private lateinit var horizonLockChip: TextView
    private lateinit var horizonPanelNote: TextView
    private lateinit var horizonGuideToggle: TextView

    private lateinit var mediaStore: MediaStoreManager
    private lateinit var photoController: PhotoController
    private lateinit var videoController: VideoController
    private lateinit var highSpeedController: HighSpeedVideoController
    private lateinit var focusController: FocusController
    private lateinit var horizon: HorizonController
    private lateinit var coordinator: CameraCoordinator
    private lateinit var haptics: Haptics

    private val zoomController = ZoomController()
    private lateinit var cameraExecutor: ExecutorService

    private var scaleDetector: ScaleGestureDetector? = null
    private var tapDetector: GestureDetector? = null
    private var pendingHighSpeedStart = false
    private var highSpeedSurfaceReady = false
    private var latestCorrection: HorizonCorrection? = null

    private val cameraPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            viewModel.mutate { it.copy(permissionCameraGranted = granted) }
            if (granted) {
                startCamera()
            } else {
                showCameraPermissionRationale()
            }
        }

    private val audioPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            viewModel.mutate {
                it.copy(
                    audioPermissionGranted = granted,
                    audioRequested = granted,
                    audioPermanentlyDenied = !granted &&
                        !shouldShowRequestPermissionRationale(Manifest.permission.RECORD_AUDIO),
                )
            }
            showNotice(
                getString(
                    if (granted) R.string.mic_on else R.string.notice_audio_denied,
                ),
            )
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        bindViews()

        mediaStore = MediaStoreManager(applicationContext)
        photoController = PhotoController(applicationContext, mediaStore)
        videoController = VideoController(applicationContext, mediaStore)
        highSpeedController = HighSpeedVideoController(applicationContext, mediaStore)
        focusController = FocusController(previewView)
        haptics = Haptics(applicationContext)
        cameraExecutor = Executors.newSingleThreadExecutor()
        horizon = HorizonController(applicationContext) { correction -> onCorrection(correction) }
        coordinator = CameraCoordinator(
            context = applicationContext,
            lifecycleOwner = this,
            previewView = previewView,
            viewModel = viewModel,
            horizon = horizon,
            zoomController = zoomController,
            photoController = photoController,
            videoController = videoController,
            executor = cameraExecutor,
        )

        wireControls()
        observeState()
        requestCameraPermissionIfNeeded()
    }

    private fun bindViews() {
        previewContainer = requireViewById(R.id.previewContainer)
        previewView = requireViewById(R.id.previewView)
        highSpeedPreview = requireViewById(R.id.highSpeedPreview)
        gridOverlay = requireViewById(R.id.gridOverlay)
        horizonOverlay = requireViewById(R.id.horizonOverlay)
        focusRing = requireViewById(R.id.focusRing)
        flashOverlay = requireViewById(R.id.flashOverlay)

        topPanel = requireViewById(R.id.topPanel)
        bottomPanel = requireViewById(R.id.bottomPanel)
        qualityPanel = requireViewById(R.id.qualityPanel)
        horizonPanel = requireViewById(R.id.horizonPanel)

        modeSummary = requireViewById(R.id.modeSummary)
        resolutionHint = requireViewById(R.id.resolutionHint)
        flashButton = requireViewById(R.id.flashButton)
        micButton = requireViewById(R.id.micButton)
        stabilizerButton = requireViewById(R.id.stabilizerButton)
        gridButton = requireViewById(R.id.gridButton)
        infoButton = requireViewById(R.id.infoButton)

        recordingChip = requireViewById(R.id.recordingChip)
        recDot = requireViewById(R.id.recDot)
        recTimer = requireViewById(R.id.recTimer)
        recSize = requireViewById(R.id.recSize)
        pauseButton = requireViewById(R.id.pauseButton)
        processingIndicator = requireViewById(R.id.processingIndicator)
        noticeBanner = requireViewById(R.id.noticeBanner)

        zoomValue = requireViewById(R.id.zoomValue)
        zoomSource = requireViewById(R.id.zoomSource)
        zoomLimit = requireViewById(R.id.zoomLimit)
        zoomSlider = requireViewById(R.id.zoomSlider)
        quickZoomButtons = listOf(
            requireViewById<TextView>(R.id.quickZoom1) to 1f,
            requireViewById<TextView>(R.id.quickZoom3) to 3f,
            requireViewById<TextView>(R.id.quickZoom10) to 10f,
            requireViewById<TextView>(R.id.quickZoom100) to 100f,
            requireViewById<TextView>(R.id.quickZoom1000) to 1000f,
        )

        qualityButton = requireViewById(R.id.qualityButton)
        horizonButton = requireViewById(R.id.horizonButton)
        horizonStatus = requireViewById(R.id.horizonStatus)
        photoTab = requireViewById(R.id.photoTab)
        videoTab = requireViewById(R.id.videoTab)
        thumbnailButton = requireViewById(R.id.thumbnailButton)
        shutterButton = requireViewById(R.id.shutterButton)
        saveOriginalToggle = requireViewById(R.id.saveOriginalToggle)

        qualityModeContainer = qualityPanel.requireViewById(R.id.qualityModeContainer)
        qualitySummaryResolution = qualityPanel.requireViewById(R.id.qualitySummaryResolution)
        qualitySummaryFps = qualityPanel.requireViewById(R.id.qualitySummaryFps)
        qualitySummaryType = qualityPanel.requireViewById(R.id.qualitySummaryType)
        qualitySummaryAudio = qualityPanel.requireViewById(R.id.qualitySummaryAudio)
        qualitySummaryStabilization = qualityPanel.requireViewById(R.id.qualitySummaryStabilization)
        qualitySummaryHorizon = qualityPanel.requireViewById(R.id.qualitySummaryHorizon)
        qualityFreeSpace = qualityPanel.requireViewById(R.id.qualityFreeSpace)
        playbackTitle = qualityPanel.requireViewById(R.id.playbackTitle)
        playbackRow = qualityPanel.requireViewById(R.id.playbackRow)
        playbackRealTime = qualityPanel.requireViewById(R.id.playbackRealTime)
        playbackSlowMotion = qualityPanel.requireViewById(R.id.playbackSlowMotion)

        horizonOffChip = horizonPanel.requireViewById(R.id.horizonOffChip)
        horizonLevelChip = horizonPanel.requireViewById(R.id.horizonLevelChip)
        horizonLockChip = horizonPanel.requireViewById(R.id.horizonLockChip)
        horizonPanelNote = horizonPanel.requireViewById(R.id.horizonPanelNote)
        horizonGuideToggle = horizonPanel.requireViewById(R.id.horizonGuideToggle)
    }

    private fun wireControls() {
        photoTab.setOnClickListener { switchMode(CaptureMode.PHOTO) }
        videoTab.setOnClickListener { switchMode(CaptureMode.VIDEO) }
        shutterButton.setOnClickListener { onShutter() }
        infoButton.setOnClickListener { startActivity(Intent(this, InfoActivity::class.java)) }
        gridButton.setOnClickListener {
            viewModel.mutate { it.copy(gridVisible = !it.gridVisible) }
        }
        flashButton.setOnClickListener { cycleFlashOrTorch() }
        micButton.setOnClickListener { toggleMicrophone() }
        stabilizerButton.setOnClickListener { toggleStabilization() }
        thumbnailButton.setOnClickListener { openLastMedia() }
        pauseButton.setOnClickListener { togglePause() }
        saveOriginalToggle.setOnClickListener {
            viewModel.mutate { it.copy(saveOriginalToo = !it.saveOriginalToo) }
        }

        qualityButton.setOnClickListener { togglePanel(qualityPanel, true) }
        horizonButton.setOnClickListener { togglePanel(horizonPanel, true) }
        qualityPanel.requireViewById<View>(R.id.qualityCloseButton).setOnClickListener {
            togglePanel(qualityPanel, false)
        }
        horizonPanel.requireViewById<View>(R.id.horizonCloseButton).setOnClickListener {
            togglePanel(horizonPanel, false)
        }

        horizonOffChip.setOnClickListener { coordinator.setHorizonMode(HorizonMode.OFF) }
        horizonLevelChip.setOnClickListener { coordinator.setHorizonMode(HorizonMode.LEVELING) }
        horizonLockChip.setOnClickListener { coordinator.setHorizonMode(HorizonMode.LOCK) }
        horizonGuideToggle.setOnClickListener {
            viewModel.mutate { it.copy(horizonGuideVisible = !it.horizonGuideVisible) }
        }

        playbackRealTime.setOnClickListener {
            viewModel.mutate { it.copy(highSpeedPlayback = HighSpeedPlayback.REAL_TIME) }
        }
        playbackSlowMotion.setOnClickListener {
            viewModel.mutate { it.copy(highSpeedPlayback = HighSpeedPlayback.SLOW_MOTION) }
        }

        zoomSlider.listener = ZoomSlider.OnRatioChanged { ratio, fromUser ->
            if (fromUser) applyZoom(ratio)
        }
        for ((button, ratio) in quickZoomButtons) {
            button.contentDescription = getString(
                R.string.zoom_quick_description,
                ZoomMath.formatRatio(ratio),
            )
            button.setOnClickListener { applyZoom(ratio) }
        }

        setUpGestures()
        setUpHighSpeedSurface()
    }

    private fun setUpGestures() {
        scaleDetector = ScaleGestureDetector(
            this,
            object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
                override fun onScale(detector: ScaleGestureDetector): Boolean {
                    val factor = ZoomMath.sanitize(detector.scaleFactor, 1f)
                    applyZoom(viewModel.current.requestedZoom * factor)
                    return true
                }
            },
        )
        tapDetector = GestureDetector(
            this,
            object : GestureDetector.SimpleOnGestureListener() {
                override fun onSingleTapUp(event: MotionEvent): Boolean {
                    focusAt(event.x, event.y)
                    return true
                }
            },
        )
        previewContainer.setOnTouchListener { view, event ->
            val scaling = scaleDetector?.onTouchEvent(event) ?: false
            val scaleInProgress = scaleDetector?.isInProgress ?: false
            // A pinch must never be treated as a tap to focus.
            if (!scaleInProgress) {
                tapDetector?.onTouchEvent(event)
            }
            if (event.actionMasked == MotionEvent.ACTION_UP) {
                view.performClick()
            }
            scaling || true
        }
    }

    private fun setUpHighSpeedSurface() {
        highSpeedPreview.holder.addCallback(
            object : SurfaceHolder.Callback {
                override fun surfaceCreated(holder: SurfaceHolder) {
                    highSpeedSurfaceReady = true
                    if (pendingHighSpeedStart) {
                        pendingHighSpeedStart = false
                        beginHighSpeedRecording(holder.surface)
                    }
                }

                override fun surfaceChanged(
                    holder: SurfaceHolder,
                    format: Int,
                    width: Int,
                    height: Int,
                ) = Unit

                override fun surfaceDestroyed(holder: SurfaceHolder) {
                    highSpeedSurfaceReady = false
                }
            },
        )
    }

    private fun observeState() {
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                launch { viewModel.state.collect { render(it) } }
                launch { viewModel.notices.collect { showNotice(it) } }
            }
        }
    }

    private fun requestCameraPermissionIfNeeded() {
        val granted = ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) ==
            PackageManager.PERMISSION_GRANTED
        viewModel.mutate {
            it.copy(
                permissionCameraGranted = granted,
                audioPermissionGranted = ContextCompat.checkSelfPermission(
                    this,
                    Manifest.permission.RECORD_AUDIO,
                ) == PackageManager.PERMISSION_GRANTED,
            )
        }
        if (granted) {
            startCamera()
        } else {
            cameraPermissionLauncher.launch(Manifest.permission.CAMERA)
        }
    }

    /**
     * Explains why the camera is needed and offers the only two useful actions:
     * ask again, or open the system settings when the request is blocked.
     */
    private fun showCameraPermissionRationale() {
        val canAskAgain = shouldShowRequestPermissionRationale(Manifest.permission.CAMERA)
        AlertDialog.Builder(this)
            .setTitle(R.string.permission_camera_title)
            .setMessage(R.string.permission_camera_body)
            .setCancelable(false)
            .setPositiveButton(
                if (canAskAgain) R.string.permission_grant else R.string.permission_settings,
            ) { dialog, _ ->
                dialog.dismiss()
                if (canAskAgain) {
                    cameraPermissionLauncher.launch(Manifest.permission.CAMERA)
                } else {
                    openApplicationSettings()
                }
            }
            .setNegativeButton(R.string.info_close) { dialog, _ -> dialog.dismiss() }
            .show()
    }

    private fun openApplicationSettings() {
        val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
            data = Uri.fromParts("package", packageName, null)
        }
        if (intent.resolveActivity(packageManager) != null) {
            startActivity(intent)
        } else {
            showNotice(getString(R.string.error_camera_permission))
        }
    }

    private fun startCamera() {
        coordinator.initialize {
            horizon.displayRotationDegrees = displayRotationDegrees()
            applyZoom(viewModel.current.requestedZoom)
        }
    }

    private fun displayRotationDegrees(): Int = when (display?.rotation) {
        Surface.ROTATION_90 -> 90
        Surface.ROTATION_180 -> 180
        Surface.ROTATION_270 -> 270
        else -> 0
    }

    // ---------------------------------------------------------------- actions

    private fun switchMode(mode: CaptureMode) {
        if (viewModel.current.mode == mode) return
        if (!viewModel.current.canSwitchMode) {
            showNotice(getString(R.string.notice_mode_busy))
            return
        }
        togglePanel(qualityPanel, false)
        togglePanel(horizonPanel, false)
        focusController.cancel(coordinator.activeCamera)
        focusRing.hide()
        coordinator.setCaptureMode(mode)
    }

    private fun applyZoom(requested: Float) {
        val previous = viewModel.current.requestedZoom
        coordinator.setZoom(requested)
        val current = viewModel.current.requestedZoom
        if (ZoomMath.stopCrossed(previous, current) != null) {
            haptics.tick()
        }
    }

    private fun focusAt(x: Float, y: Float) {
        if (!viewModel.current.cameraReady) return
        focusRing.show(x, y)
        val started = focusController.focusAt(
            coordinator.activeCamera,
            x,
            y,
            viewModel.current.digitalFactor,
        ) { successful -> focusRing.finish(successful) }
        if (!started) {
            focusRing.finish(false)
            showNotice(getString(R.string.error_focus))
        }
    }

    private fun onShutter() {
        when (viewModel.current.mode) {
            CaptureMode.PHOTO -> capturePhoto()
            CaptureMode.VIDEO -> toggleRecording()
        }
    }

    private fun capturePhoto() {
        val capture = coordinator.currentImageCapture ?: return
        val state = viewModel.current
        if (state.captureStatus != CaptureStatus.IDLE) return
        viewModel.mutate { it.copy(captureStatus = CaptureStatus.CAPTURING) }
        playShutterFlash()
        lifecycleScope.launch {
            val result = photoController.capture(
                imageCapture = capture,
                executor = cameraExecutor,
                digitalFactor = state.digitalFactor,
                advertisedSize = coordinator.advertisedPhotoSize,
                saveOriginalToo = state.saveOriginalToo,
            )
            when (result) {
                is PhotoResult.Saved -> {
                    viewModel.mutate {
                        it.copy(
                            captureStatus = CaptureStatus.IDLE,
                            lastPhotoSize = result.savedSize,
                            lastPhotoVendorLimited = result.vendorLimited,
                            lastMediaUri = result.uri,
                            lastMediaIsVideo = false,
                        )
                    }
                    haptics.confirm()
                    showNotice(
                        if (result.digitalFactor > 1.001f) {
                            result.description + " · " + getString(
                                R.string.photo_digital_note,
                                String.format(java.util.Locale.US, "%.1f", result.digitalFactor),
                            ) + if (result.originalUri != null) {
                                " · " + getString(R.string.save_original)
                            } else {
                                ""
                            }
                        } else {
                            result.description
                        },
                    )
                    if (result.vendorLimited) {
                        showNotice(
                            getString(
                                R.string.photo_vendor_limited,
                                MegapixelMath.shortDescribe(
                                    result.savedSize.width,
                                    result.savedSize.height,
                                ),
                            ),
                        )
                    }
                    loadThumbnail(result.uri, isVideo = false)
                }

                is PhotoResult.Failed -> {
                    viewModel.mutate { it.copy(captureStatus = CaptureStatus.IDLE) }
                    showNotice(getString(R.string.error_capture, result.cause))
                }
            }
        }
    }

    private fun toggleRecording() {
        val state = viewModel.current
        val availability = state.selectedVideoAvailability
        if (availability == null || !availability.available) {
            showNotice(getString(R.string.recording_unavailable))
            return
        }
        if (state.isRecording) {
            stopRecording()
            return
        }
        if (availability.mode.kind == RecordingKind.HIGH_SPEED) {
            startHighSpeedRecording(availability)
        } else {
            startNormalRecording(availability)
        }
    }

    private fun startNormalRecording(availability: VideoModeAvailability) {
        val capture = coordinator.currentVideoCapture ?: run {
            showNotice(getString(R.string.error_bind_failed))
            return
        }
        val state = viewModel.current
        if (state.audioRequested && !state.audioPermissionGranted) {
            audioPermissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
            return
        }
        viewModel.mutate { it.copy(recordingStatus = RecordingStatus.STARTING) }
        val started = videoController.start(
            videoCapture = capture,
            mode = availability.mode,
            audioEnabled = state.audioRequested && state.audioPermissionGranted,
            listener = object : RecordingListener {
                override fun onStarted() {
                    viewModel.mutate {
                        it.copy(recordingStatus = RecordingStatus.RECORDING, recordedMillis = 0L)
                    }
                    haptics.confirm()
                }

                override fun onStatus(durationMillis: Long, bytes: Long) {
                    viewModel.mutate {
                        it.copy(recordedMillis = durationMillis, recordedBytes = bytes)
                    }
                }

                override fun onPaused() {
                    viewModel.mutate { it.copy(recordingStatus = RecordingStatus.PAUSED) }
                }

                override fun onResumed() {
                    viewModel.mutate { it.copy(recordingStatus = RecordingStatus.RECORDING) }
                }

                override fun onFinished(uri: Uri?, errorMessage: String?) {
                    viewModel.mutate {
                        it.copy(
                            recordingStatus = RecordingStatus.IDLE,
                            lastMediaUri = uri ?: it.lastMediaUri,
                            lastMediaIsVideo = uri != null,
                            freeBytes = mediaStore.freeBytes(),
                        )
                    }
                    if (errorMessage != null) {
                        showNotice(errorMessage)
                    } else {
                        showNotice(getString(R.string.notice_saved_video))
                        uri?.let { loadThumbnail(it, isVideo = true) }
                    }
                }
            },
        )
        if (!started) {
            viewModel.mutate { it.copy(recordingStatus = RecordingStatus.IDLE) }
            showNotice(getString(R.string.error_no_space))
        }
    }

    private fun startHighSpeedRecording(availability: VideoModeAvailability) {
        val cameraId = coordinator.cameraId ?: run {
            showNotice(getString(R.string.error_camera_lost))
            return
        }
        viewModel.mutate { it.copy(recordingStatus = RecordingStatus.STARTING) }
        // The Camera2 session needs exclusive access, so CameraX has to let go.
        coordinator.releaseCameraX()
        previewView.visibility = View.INVISIBLE
        highSpeedPreview.visibility = View.VISIBLE
        highSpeedPreview.holder.setFixedSize(
            availability.mode.size.width,
            availability.mode.size.height,
        )
        if (highSpeedSurfaceReady) {
            beginHighSpeedRecording(highSpeedPreview.holder.surface)
        } else {
            pendingHighSpeedStart = true
        }
    }

    private fun beginHighSpeedRecording(surface: Surface) {
        val state = viewModel.current
        val availability = state.selectedVideoAvailability ?: return
        val started = highSpeedController.start(
            cameraId = coordinator.cameraId.orEmpty(),
            sensorOrientation = coordinator.sensorOrientation,
            mode = availability.mode,
            playback = state.highSpeedPlayback,
            previewSurface = surface,
            listener = object : HighSpeedVideoController.Listener {
                override fun onStarted() {
                    runOnUiThread {
                        viewModel.mutate {
                            it.copy(recordingStatus = RecordingStatus.RECORDING, recordedMillis = 0)
                        }
                        haptics.confirm()
                    }
                }

                override fun onStatus(durationMillis: Long) {
                    runOnUiThread {
                        viewModel.mutate { it.copy(recordedMillis = durationMillis) }
                    }
                }

                override fun onFinished(uri: Uri?, errorMessage: String?) {
                    runOnUiThread { finishHighSpeed(uri, errorMessage) }
                }
            },
        )
        if (!started) {
            // The controller already reported the reason through the listener.
            pendingHighSpeedStart = false
        }
    }

    private fun finishHighSpeed(uri: Uri?, errorMessage: String?) {
        viewModel.mutate {
            it.copy(
                recordingStatus = RecordingStatus.IDLE,
                lastMediaUri = uri ?: it.lastMediaUri,
                lastMediaIsVideo = uri != null,
                freeBytes = mediaStore.freeBytes(),
            )
        }
        highSpeedPreview.visibility = View.GONE
        previewView.visibility = View.VISIBLE
        coordinator.rebind()
        if (errorMessage != null) {
            showNotice(errorMessage)
        } else {
            showNotice(getString(R.string.notice_saved_video))
            uri?.let { loadThumbnail(it, isVideo = true) }
        }
    }

    private fun stopRecording() {
        viewModel.mutate { it.copy(recordingStatus = RecordingStatus.STOPPING) }
        if (highSpeedController.isRecording) {
            highSpeedController.stop()
        } else {
            videoController.stop()
        }
    }

    /** Pause and resume are only offered for the CameraX recorder. */
    private fun togglePause() {
        val state = viewModel.current
        if (state.selectedMode.kind == RecordingKind.HIGH_SPEED) {
            showNotice(getString(R.string.recording_pause_unavailable))
            return
        }
        when (state.recordingStatus) {
            RecordingStatus.RECORDING -> videoController.pause()
            RecordingStatus.PAUSED -> videoController.resume()
            else -> Unit
        }
    }

    private fun cycleFlashOrTorch() {
        val state = viewModel.current
        if (!state.hasFlashUnit) {
            showNotice(getString(R.string.flash_unavailable))
            return
        }
        if (state.mode == CaptureMode.VIDEO) {
            coordinator.setTorch(!state.torchOn)
            return
        }
        val next = when (state.flashMode) {
            FlashMode.AUTO -> FlashMode.ON
            FlashMode.ON -> FlashMode.OFF
            FlashMode.OFF -> FlashMode.AUTO
        }
        coordinator.setFlashMode(next)
    }

    private fun toggleMicrophone() {
        val state = viewModel.current
        if (state.audioRequested) {
            viewModel.mutate { it.copy(audioRequested = false) }
            return
        }
        if (state.audioPermissionGranted) {
            viewModel.mutate { it.copy(audioRequested = true) }
            return
        }
        if (state.audioPermanentlyDenied) {
            showNotice(getString(R.string.notice_audio_denied_forever))
            return
        }
        audioPermissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
    }

    private fun toggleStabilization() {
        val state = viewModel.current
        if (!state.stabilizationSupported) {
            showNotice(getString(R.string.stabilizer_unavailable))
            return
        }
        coordinator.setStabilization(!state.stabilizationRequested)
    }

    private fun openLastMedia() {
        val uri = viewModel.current.lastMediaUri ?: return
        if (!mediaStore.exists(uri)) {
            showNotice(getString(R.string.notice_media_missing))
            viewModel.mutate { it.copy(lastMediaUri = null) }
            return
        }
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(
                uri,
                if (viewModel.current.lastMediaIsVideo) "video/mp4" else "image/jpeg",
            )
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        if (intent.resolveActivity(packageManager) != null) {
            startActivity(intent)
        } else {
            showNotice(getString(R.string.notice_no_viewer))
        }
    }

    private fun loadThumbnail(uri: Uri, isVideo: Boolean) {
        lifecycleScope.launch {
            val bitmap: Bitmap? = withContext(Dispatchers.IO) {
                try {
                    contentResolver.loadThumbnail(uri, Size(128, 128), null)
                } catch (e: IOException) {
                    null
                } catch (e: SecurityException) {
                    null
                }
            }
            if (bitmap != null) {
                thumbnailButton.setImageBitmap(bitmap)
                thumbnailButton.setPadding(0, 0, 0, 0)
            }
            viewModel.mutate { it.copy(lastMediaUri = uri, lastMediaIsVideo = isVideo) }
        }
    }

    // ---------------------------------------------------------------- render

    private fun onCorrection(correction: HorizonCorrection) {
        runOnUiThread {
            if (isFinishing || isDestroyed) return@runOnUiThread
            latestCorrection = correction
            horizonOverlay.update(
                correction.measuredRoll,
                correction,
                viewModel.current.horizonMode,
            )
            updateHorizonStatusText(viewModel.current)
        }
    }

    /** The angle, the state and the crop cost, refreshed with the sensor. */
    private fun updateHorizonStatusText(state: CameraUiState) {
        val correction = latestCorrection
        horizonStatus.text = buildString {
            append(getString(horizonOverlay.statusTextRes()))
            append(" · ")
            append(getString(R.string.horizon_angle, horizonOverlay.angleText()))
            if (correction != null && state.horizonMode != HorizonMode.OFF) {
                append(" · ")
                append(
                    getString(
                        R.string.horizon_crop,
                        String.format(java.util.Locale.US, "%.0f", correction.croppedPercent),
                    ),
                )
            }
        }
    }

    private fun render(state: CameraUiState) {
        renderTopPanel(state)
        renderZoom(state)
        renderModeSelector(state)
        renderShutter(state)
        renderRecording(state)
        renderHorizon(state)
        renderQualityPanel(state)
        gridOverlay.visibility = if (state.gridVisible) View.VISIBLE else View.GONE

        val heavy = state.mode == CaptureMode.VIDEO &&
            (state.selectedMode.fps >= 60 || state.selectedMode.size.area >= 3840L * 2160L ||
                state.horizonMode != HorizonMode.OFF)
        topPanel.lightweight = heavy
        bottomPanel.lightweight = heavy

        val scale = if (state.mode == CaptureMode.PHOTO) state.digitalFactor else 1f
        previewView.scaleX = scale
        previewView.scaleY = scale

        val processingLabel = when (state.captureStatus) {
            CaptureStatus.PROCESSING -> getString(R.string.capture_processing)
            CaptureStatus.SAVING, CaptureStatus.CAPTURING -> getString(R.string.capture_saving)
            CaptureStatus.IDLE -> null
        }
        processingIndicator.visibility = if (processingLabel != null) View.VISIBLE else View.GONE
        processingIndicator.contentDescription = processingLabel
    }

    private fun renderTopPanel(state: CameraUiState) {
        modeSummary.text = if (state.mode == CaptureMode.PHOTO) {
            getString(R.string.label_max_mp)
        } else {
            state.selectedMode.label
        }
        resolutionHint.text = buildPhotoHint(state)

        flashButton.isEnabled = state.hasFlashUnit
        flashButton.alpha = if (state.hasFlashUnit) 1f else 0.4f
        flashButton.setImageResource(
            when {
                state.mode == CaptureMode.VIDEO && state.torchOn -> R.drawable.ic_flash_on
                state.mode == CaptureMode.VIDEO -> R.drawable.ic_flash_off
                state.flashMode == FlashMode.AUTO -> R.drawable.ic_flash_auto
                state.flashMode == FlashMode.ON -> R.drawable.ic_flash_on
                else -> R.drawable.ic_flash_off
            },
        )
        flashButton.contentDescription = when {
            !state.hasFlashUnit -> getString(R.string.flash_unavailable)
            state.mode == CaptureMode.VIDEO ->
                getString(if (state.torchOn) R.string.torch_on else R.string.torch_off)

            state.flashMode == FlashMode.AUTO -> getString(R.string.flash_auto)
            state.flashMode == FlashMode.ON -> getString(R.string.flash_on)
            else -> getString(R.string.flash_off)
        }

        micButton.setImageResource(
            if (state.audioRequested && state.audioPermissionGranted) {
                R.drawable.ic_mic_on
            } else {
                R.drawable.ic_mic_off
            },
        )
        micButton.contentDescription =
            getString(if (state.audioRequested) R.string.mic_on else R.string.mic_off)
        micButton.visibility = if (state.mode == CaptureMode.VIDEO) View.VISIBLE else View.GONE

        stabilizerButton.visibility =
            if (state.mode == CaptureMode.VIDEO) View.VISIBLE else View.GONE
        stabilizerButton.alpha = if (state.stabilizationSupported) 1f else 0.4f
        stabilizerButton.contentDescription = when {
            !state.stabilizationSupported -> getString(R.string.stabilizer_unavailable)
            state.stabilizationRequested -> getString(R.string.stabilizer_on)
            else -> getString(R.string.stabilizer_off)
        }
    }

    /** Requested size, resolution tier and the size of the last file written. */
    private fun buildPhotoHint(state: CameraUiState): CharSequence {
        if (state.mode == CaptureMode.VIDEO) {
            return getString(
                R.string.label_photo_target,
                "${state.selectedMode.size} · ${state.selectedMode.fps} fps",
            )
        }
        val target = state.photoTargetSize
            ?: return getString(R.string.label_photo_target_unknown)
        return buildString {
            append(
                getString(
                    R.string.label_photo_target,
                    "$target · ≈${MegapixelMath.roundedMegapixels(target.width, target.height)} MP",
                ),
            )
            append(" · ")
            append(
                getString(
                    if (state.photoTargetIsHighResolution) {
                        R.string.label_high_resolution
                    } else {
                        R.string.label_standard_resolution
                    },
                ),
            )
            if (state.photoDegraded) {
                append(" · ")
                append(getString(R.string.notice_resolution_degraded))
            }
            state.lastPhotoSize?.let {
                append("\n")
                append(MegapixelMath.describe(it.width, it.height))
                if (state.lastPhotoVendorLimited) {
                    append(" · ")
                    append(getString(R.string.label_standard_resolution))
                }
            }
        }
    }

    private fun renderZoom(state: CameraUiState) {
        zoomValue.text = ZoomMath.formatRatio(state.requestedZoom)
        zoomValue.contentDescription =
            getString(R.string.zoom_value_description, ZoomMath.formatRatio(state.requestedZoom))
        zoomSource.setText(
            if (state.zoomSource == ZoomSource.DIGITAL) {
                R.string.zoom_source_digital
            } else {
                R.string.zoom_source_camera
            },
        )
        zoomLimit.text =
            getString(R.string.zoom_camera_limit, ZoomMath.formatRatio(state.cameraMaxZoom))
        zoomSlider.setRange(state.cameraMinZoom, state.maxSelectableZoom, state.cameraMaxZoom)
        zoomSlider.setRatioSilently(state.requestedZoom)

        for ((button, ratio) in quickZoomButtons) {
            val reachable = ratio <= state.maxSelectableZoom + 0.001f
            button.isEnabled = reachable
            button.alpha = if (reachable) 1f else 0.35f
            button.setBackgroundResource(
                if (kotlin.math.abs(state.requestedZoom - ratio) < 0.05f) {
                    R.drawable.bg_chip_selected
                } else {
                    R.drawable.bg_chip
                },
            )
        }
        saveOriginalToggle.visibility =
            if (state.mode == CaptureMode.PHOTO && state.zoomSource == ZoomSource.DIGITAL) {
                View.VISIBLE
            } else {
                View.INVISIBLE
            }
        saveOriginalToggle.setBackgroundResource(
            if (state.saveOriginalToo) R.drawable.bg_chip_selected else R.drawable.bg_chip,
        )
    }

    private fun renderModeSelector(state: CameraUiState) {
        photoTab.setBackgroundResource(
            if (state.mode == CaptureMode.PHOTO) R.drawable.bg_chip_selected else android.R.color.transparent,
        )
        videoTab.setBackgroundResource(
            if (state.mode == CaptureMode.VIDEO) R.drawable.bg_chip_selected else android.R.color.transparent,
        )
        photoTab.isEnabled = state.canSwitchMode || state.mode == CaptureMode.PHOTO
        videoTab.isEnabled = state.canSwitchMode || state.mode == CaptureMode.VIDEO
        qualityButton.visibility = if (state.mode == CaptureMode.VIDEO) View.VISIBLE else View.GONE
        qualityButton.text = state.selectedMode.label
    }

    private fun renderShutter(state: CameraUiState) {
        val look = when {
            state.mode == CaptureMode.PHOTO -> ShutterButton.Look.PHOTO
            state.isRecording -> ShutterButton.Look.VIDEO_RECORDING
            else -> ShutterButton.Look.VIDEO_IDLE
        }
        shutterButton.setLook(look)
        val enabled = state.cameraReady &&
            state.captureStatus == CaptureStatus.IDLE &&
            state.recordingStatus != RecordingStatus.STARTING &&
            state.recordingStatus != RecordingStatus.STOPPING
        shutterButton.isEnabled = enabled || state.isRecording
    }

    private fun renderRecording(state: CameraUiState) {
        val visible = state.isRecording
        recordingChip.visibility = if (visible) View.VISIBLE else View.GONE
        val canPause = visible && state.selectedMode.kind == RecordingKind.NORMAL
        pauseButton.visibility = if (canPause) View.VISIBLE else View.GONE
        pauseButton.setText(
            if (state.recordingStatus == RecordingStatus.PAUSED) {
                R.string.recording_resume
            } else {
                R.string.recording_pause
            },
        )
        recDot.alpha = if (state.recordingStatus == RecordingStatus.PAUSED) 0.35f else 1f
        if (!visible) return
        recTimer.text = UiStrings.duration(state.recordedMillis)
        recSize.text = if (state.recordedBytes > 0) {
            UiStrings.bytes(state.recordedBytes)
        } else {
            getString(R.string.free_space, UiStrings.bytes(state.freeBytes))
        }
    }

    private fun renderHorizon(state: CameraUiState) {
        horizonButton.text = UiStrings.horizonLabel(this, state.horizonMode)
        horizonOverlay.visibility =
            if (state.horizonGuideVisible && state.horizonAvailableOnDevice) {
                View.VISIBLE
            } else {
                View.GONE
            }
        horizonOverlay.update(horizon.measuredRoll, latestCorrection, state.horizonMode)
        updateHorizonStatusText(state)

        val support = state.horizonSupport
        horizonOffChip.setBackgroundResource(chipBackground(state.horizonMode == HorizonMode.OFF, true))
        horizonLevelChip.setBackgroundResource(
            chipBackground(state.horizonMode == HorizonMode.LEVELING, support.levelingAvailable),
        )
        horizonLockChip.setBackgroundResource(
            chipBackground(state.horizonMode == HorizonMode.LOCK, support.lockAvailable),
        )
        horizonLevelChip.isEnabled = support.levelingAvailable
        horizonLockChip.isEnabled = support.lockAvailable
        horizonLevelChip.alpha = if (support.levelingAvailable) 1f else 0.45f
        horizonLockChip.alpha = if (support.lockAvailable) 1f else 0.45f

        horizonPanelNote.text = when {
            !state.horizonAvailableOnDevice -> getString(R.string.horizon_no_sensor)
            state.mode == CaptureMode.PHOTO -> getString(R.string.horizon_photo_only_guide)
            UiStrings.horizonBlockReason(this, support.blockReason) != null ->
                UiStrings.horizonBlockReason(this, support.blockReason)

            support.experimental -> getString(R.string.horizon_experimental_note)
            else -> ""
        }
        horizonGuideToggle.setText(
            if (state.horizonGuideVisible) R.string.horizon_guide_hide else R.string.horizon_guide_show,
        )
    }

    private fun renderQualityPanel(state: CameraUiState) {
        if (qualityModeContainer.childCount != state.videoModes.size) {
            qualityModeContainer.removeAllViews()
            repeat(state.videoModes.size) {
                layoutInflater.inflate(R.layout.item_quality_mode, qualityModeContainer, true)
            }
        }
        state.videoModes.forEachIndexed { index, availability ->
            val row = qualityModeContainer.getChildAt(index) ?: return@forEachIndexed
            val label = row.requireViewById<TextView>(R.id.qualityItemLabel)
            val reason = row.requireViewById<TextView>(R.id.qualityItemReason)
            val badge = row.requireViewById<TextView>(R.id.qualityItemBadge)
            label.text = availability.mode.label
            badge.setText(
                when {
                    !availability.available -> R.string.recording_unavailable
                    availability.isHighSpeed -> R.string.recording_high_speed
                    else -> R.string.recording_normal
                },
            )
            if (availability.available) {
                reason.visibility = View.GONE
            } else {
                reason.visibility = View.VISIBLE
                reason.text = UiStrings.reason(this, availability)
            }
            val selected = availability.mode.id == state.selectedVideoMode
            row.setBackgroundResource(chipBackground(selected, availability.available))
            row.isEnabled = availability.available
            row.alpha = if (availability.available) 1f else 0.55f
            row.contentDescription = "${availability.mode.label} ${badge.text}"
            row.setOnClickListener {
                if (availability.available) {
                    coordinator.setVideoMode(availability.mode.id)
                } else {
                    showNotice(UiStrings.reason(this, availability))
                }
            }
        }

        val availability = state.selectedVideoAvailability
        val mode = state.selectedMode
        qualitySummaryResolution.text = "${getString(R.string.resolution_row)}: ${mode.size}"
        qualitySummaryFps.text = "${getString(R.string.fps_row)}: ${mode.fps}"
        qualitySummaryType.text = getString(R.string.type_row) + ": " + getString(
            if (mode.kind == RecordingKind.HIGH_SPEED) {
                R.string.recording_high_speed
            } else {
                R.string.recording_normal
            },
        )
        qualitySummaryAudio.text = getString(R.string.audio_row) + ": " + when {
            availability?.audioSupported == false -> getString(R.string.audio_high_speed_unavailable)
            state.audioRequested && state.audioPermissionGranted -> getString(R.string.mic_on)
            else -> getString(R.string.mic_off)
        }
        qualitySummaryStabilization.text = getString(R.string.stabilization_row) + ": " + when {
            availability?.stabilizationSupported != true -> getString(R.string.stabilizer_unavailable)
            state.stabilizationRequested -> getString(R.string.stabilizer_on)
            else -> getString(R.string.stabilizer_off)
        }
        qualitySummaryHorizon.text = getString(R.string.horizon_row) + ": " +
            UiStrings.horizonLabel(this, state.horizonMode)
        qualityFreeSpace.text = getString(R.string.free_space, UiStrings.bytes(state.freeBytes))

        val highSpeedSelected = mode.kind == RecordingKind.HIGH_SPEED
        playbackTitle.visibility = if (highSpeedSelected) View.VISIBLE else View.GONE
        playbackRow.visibility = if (highSpeedSelected) View.VISIBLE else View.GONE
        playbackRealTime.setBackgroundResource(
            chipBackground(state.highSpeedPlayback == HighSpeedPlayback.REAL_TIME, true),
        )
        playbackSlowMotion.setBackgroundResource(
            chipBackground(state.highSpeedPlayback == HighSpeedPlayback.SLOW_MOTION, true),
        )
    }

    private fun chipBackground(selected: Boolean, available: Boolean): Int = when {
        !available -> R.drawable.bg_chip_disabled
        selected -> R.drawable.bg_chip_selected
        else -> R.drawable.bg_chip
    }

    private fun togglePanel(panel: View, show: Boolean) {
        if (show) {
            if (panel === qualityPanel) Motion.fade(horizonPanel, false)
            if (panel === horizonPanel) Motion.fade(qualityPanel, false)
            viewModel.mutate { it.copy(freeBytes = mediaStore.freeBytes()) }
        }
        Motion.fade(panel, show)
    }

    private fun playShutterFlash() {
        val duration = Motion.scaled(this, Motion.FAST)
        if (duration == 0L) return
        flashOverlay.alpha = 0.85f
        flashOverlay.animate()
            .alpha(0f)
            .setDuration(duration)
            .setInterpolator(Motion.STANDARD)
            .start()
    }

    private fun showNotice(notice: UiNotice) {
        val text = when (notice) {
            is UiNotice.Resource -> getString(
                notice.stringRes,
                *notice.formatArgs.toTypedArray(),
            )

            is UiNotice.Text -> notice.message
            is UiNotice.SavedPhoto -> notice.description
        }
        showNotice(text)
    }

    private fun showNotice(text: String) {
        if (text.isBlank()) return
        noticeBanner.text = text
        Motion.fade(noticeBanner, true, Motion.FAST)
        noticeBanner.removeCallbacks(hideNotice)
        noticeBanner.postDelayed(hideNotice, NOTICE_MILLIS)
        noticeBanner.announceForAccessibility(text)
    }

    private val hideNotice = Runnable { Motion.fade(noticeBanner, false, Motion.FAST) }

    // ------------------------------------------------------------- lifecycle

    override fun onStart() {
        super.onStart()
        horizon.displayRotationDegrees = displayRotationDegrees()
        horizon.start()
        viewModel.mutate { it.copy(freeBytes = mediaStore.freeBytes()) }
    }

    override fun onStop() {
        // A recording that survives into the background would produce a broken
        // file, so it is finalised here instead.
        if (viewModel.current.isRecording) {
            stopRecording()
        }
        videoController.finishForLifecycle()
        highSpeedController.release()
        horizon.stop()
        super.onStop()
    }

    override fun onDestroy() {
        noticeBanner.removeCallbacks(hideNotice)
        coordinator.shutdown()
        cameraExecutor.shutdown()
        super.onDestroy()
    }

    private companion object {
        const val NOTICE_MILLIS = 3200L
    }
}
