package com.flex.cameradev.camera

import android.content.Context
import android.util.Log
import android.util.Size
import androidx.camera.core.Camera
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageCapture
import androidx.camera.core.Preview
import androidx.camera.core.UseCase
import androidx.camera.core.UseCaseGroup
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.video.Recorder
import androidx.camera.video.VideoCapture
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import com.flex.cameradev.R
import com.flex.cameradev.core.HorizonCompatibility
import com.flex.cameradev.core.HorizonMode
import com.flex.cameradev.core.HorizonSupport
import com.flex.cameradev.core.RecordingKind
import com.flex.cameradev.core.ResolutionSelection
import com.flex.cameradev.core.SizeSpec
import com.flex.cameradev.core.UnavailableReason
import com.flex.cameradev.core.VideoModeAvailability
import com.flex.cameradev.core.VideoModeCatalog
import com.flex.cameradev.core.VideoModeId
import com.flex.cameradev.horizon.HorizonController
import com.flex.cameradev.horizon.VideoRenderPipeline
import com.flex.cameradev.ui.CameraViewModel
import com.flex.cameradev.ui.CaptureMode
import com.flex.cameradev.ui.FlashMode
import com.flex.cameradev.ui.UiNotice
import java.util.concurrent.ExecutionException
import java.util.concurrent.Executor
import kotlin.math.abs

/**
 * Owns the CameraX session.
 *
 * Binding happens in exactly one place, so mode changes, resolution fallbacks
 * and the horizon effect cannot end up fighting over the same use cases.
 */
class CameraCoordinator(
    private val context: Context,
    private val lifecycleOwner: LifecycleOwner,
    private val previewView: PreviewView,
    private val viewModel: CameraViewModel,
    private val horizon: HorizonController,
    private val zoomController: ZoomController,
    private val photoController: PhotoController,
    private val videoController: VideoController,
    private val executor: Executor,
) {

    private var provider: ProcessCameraProvider? = null
    private var camera: Camera? = null

    private var preview: Preview? = null
    private var imageCapture: ImageCapture? = null
    private var videoCapture: VideoCapture<Recorder>? = null

    private var boundUseCases: List<UseCase> = emptyList()
    private var effectAttached = false

    /** The recorder capabilities only have to be cross-checked once per camera. */
    private var qualitiesRefined = false

    private val pipeline = VideoRenderPipeline(horizon) { throwable ->
        Log.e(TAG, "El efecto de horizonte falló", throwable)
        viewModel.notify(UiNotice.Resource(R.string.error_horizon_pipeline))
        val support = currentHorizonSupport()
        horizon.setMode(HorizonMode.OFF, support)
        viewModel.mutate { it.copy(horizonMode = HorizonMode.OFF, horizonSupport = support) }
        rebind()
    }

    private var jpegCandidates: List<SizeSpec> = emptyList()
    private var photoTarget: SizeSpec? = null
    private var advertisedJpegSize: SizeSpec? = null

    val activeCamera: Camera? get() = camera
    val currentVideoCapture: VideoCapture<Recorder>? get() = videoCapture
    val currentImageCapture: ImageCapture? get() = imageCapture
    val advertisedPhotoSize: SizeSpec? get() = advertisedJpegSize
    val cameraId: String? get() = viewModel.report?.mainCameraId
    val sensorOrientation: Int get() = viewModel.report?.mainProbe?.sensorOrientation ?: 90

    /** Reads the device metadata once and prepares the first binding. */
    fun initialize(onReady: () -> Unit) {
        val report = viewModel.report ?: CameraCapabilities.probeDevice(context).also {
            viewModel.report = it
        }
        val probe = report.mainProbe
        jpegCandidates = ResolutionSelection.orderedJpegCandidates(
            probe?.jpegSizes.orEmpty(),
            probe?.highResolutionJpegSizes.orEmpty(),
        )
        photoTarget = jpegCandidates.firstOrNull()
        advertisedJpegSize = photoTarget
        val availability = VideoModeProbe.evaluate(probe)

        viewModel.mutate { state ->
            val resolved = VideoModeCatalog.fallbackFor(state.selectedVideoMode, availability)
            state.copy(
                photoTargetSize = photoTarget,
                photoTargetIsHighResolution = probe?.highResolutionJpegSizes
                    ?.any { it == photoTarget } == true,
                videoModes = availability,
                selectedVideoMode = resolved?.mode?.id ?: state.selectedVideoMode,
                hasFlashUnit = probe?.hasFlash == true,
                stabilizationSupported = probe?.supportsVideoStabilization == true,
                horizonAvailableOnDevice = horizon.isSensorAvailable,
                cameraMaxZoom = probe?.zoomRange?.endInclusive ?: probe?.maxDigitalZoom ?: 1f,
            )
        }
        horizon.setFrameSize(viewModel.current.selectedMode.size)

        val future = ProcessCameraProvider.getInstance(context)
        future.addListener(
            {
                try {
                    provider = future.get()
                    rebind()
                    onReady()
                } catch (e: ExecutionException) {
                    Log.e(TAG, "CameraX no pudo iniciarse", e)
                    viewModel.notify(UiNotice.Resource(R.string.error_camera_start))
                } catch (e: InterruptedException) {
                    Thread.currentThread().interrupt()
                }
            },
            ContextCompat.getMainExecutor(context),
        )
    }

    /** Horizon support for the mode that is selected right now. */
    fun currentHorizonSupport(): HorizonSupport {
        val state = viewModel.current
        return if (state.mode == CaptureMode.PHOTO) {
            HorizonCompatibility.photoSupport()
        } else {
            HorizonCompatibility.supportFor(
                state.selectedMode,
                pipelineAvailable = horizon.isSensorAvailable,
            )
        }
    }

    /** Switches between stills and video without ever holding two bindings. */
    fun setCaptureMode(mode: CaptureMode) {
        if (viewModel.current.mode == mode) return
        viewModel.mutate { it.copy(mode = mode, busySwitchingMode = true) }
        val support = currentHorizonSupport()
        val resolved = support.resolve(viewModel.current.horizonMode)
        horizon.setMode(resolved, support)
        viewModel.mutate { it.copy(horizonMode = resolved, horizonSupport = support) }
        // Keep the zoom when the new mode allows it, clamp it otherwise.
        setZoom(viewModel.current.requestedZoom)
        rebind()
        viewModel.mutate { it.copy(busySwitchingMode = false) }
    }

    fun setVideoMode(id: VideoModeId) {
        val availability = viewModel.current.videoModes.firstOrNull { it.mode.id == id } ?: return
        if (!availability.available) return
        viewModel.mutate { it.copy(selectedVideoMode = id) }
        applyModeConstraints(availability)
        if (viewModel.current.mode == CaptureMode.VIDEO) rebind()
    }

    /** Re-checks horizon support and the zoom ceiling after a mode change. */
    private fun applyModeConstraints(availability: VideoModeAvailability) {
        val support = HorizonCompatibility.supportFor(
            availability.mode,
            pipelineAvailable = horizon.isSensorAvailable,
        )
        val requested = viewModel.current.horizonMode
        val resolved = support.resolve(requested)
        if (resolved != requested) {
            viewModel.notify(UiNotice.Resource(R.string.notice_horizon_downgraded))
        }
        horizon.setFrameSize(availability.mode.size)
        horizon.setMode(resolved, support)
        viewModel.mutate { it.copy(horizonMode = resolved, horizonSupport = support) }

        val allowDigital = availability.mode.kind != RecordingKind.HIGH_SPEED
        val clamped = zoomController.clampToMode(viewModel.current.requestedZoom, allowDigital)
        if (abs(clamped - viewModel.current.requestedZoom) > 0.001f) {
            viewModel.notify(UiNotice.Resource(R.string.notice_zoom_clamped))
        }
        setZoom(clamped)
    }

    fun setHorizonMode(mode: HorizonMode) {
        val support = currentHorizonSupport()
        val resolved = support.resolve(mode)
        horizon.setMode(resolved, support)
        viewModel.mutate { it.copy(horizonMode = resolved, horizonSupport = support) }
        if (resolved != mode) {
            viewModel.notify(UiNotice.Resource(R.string.notice_horizon_unavailable))
        }
        if (viewModel.current.mode == CaptureMode.VIDEO) rebind()
    }

    fun setZoom(requestedRatio: Float) {
        val state = viewModel.current
        val allowDigital = state.mode == CaptureMode.PHOTO ||
            state.selectedMode.kind != RecordingKind.HIGH_SPEED
        val clamped = zoomController.clampToMode(requestedRatio, allowDigital)
        val plan = zoomController.apply(clamped)
        val effectWasNeeded = effectAttached
        horizon.setDigitalZoom(if (state.mode == CaptureMode.VIDEO) plan.digitalFactor else 1f)
        viewModel.mutate {
            it.copy(
                requestedZoom = plan.requestedRatio,
                cameraZoom = plan.cameraRatio,
                digitalFactor = plan.digitalFactor,
                cameraMinZoom = zoomController.minRatio,
                cameraMaxZoom = zoomController.maxCameraRatio,
            )
        }
        // Entering or leaving the crop pipeline changes the bound use cases. A mode
        // switch rebinds anyway, so it is skipped here to avoid binding twice.
        if (!viewModel.current.busySwitchingMode &&
            viewModel.current.mode == CaptureMode.VIDEO &&
            needsEffect() != effectWasNeeded
        ) {
            rebind()
        }
    }

    fun setFlashMode(mode: FlashMode) {
        viewModel.mutate { it.copy(flashMode = mode) }
        imageCapture?.flashMode = mode.toCameraX()
    }

    fun setTorch(enabled: Boolean) {
        val info = camera?.cameraInfo ?: return
        if (!info.hasFlashUnit()) return
        camera?.cameraControl?.enableTorch(enabled)
        viewModel.mutate { it.copy(torchOn = enabled) }
    }

    fun setStabilization(enabled: Boolean) {
        viewModel.mutate { it.copy(stabilizationRequested = enabled) }
        if (viewModel.current.mode == CaptureMode.VIDEO) rebind()
    }

    /** Rebuilds the use case set and binds it, degrading the still size if refused. */
    fun rebind() {
        val cameraProvider = provider ?: return
        val selector = CameraSelectorManager.selectorFor(viewModel.report?.mainCameraId)
        var attempt = 0
        while (attempt < MAX_BIND_ATTEMPTS) {
            val wantsEffect = needsEffect()
            val useCases = buildUseCases(cameraProvider)
            try {
                unbindStale(cameraProvider, useCases, wantsEffect)
                val builder = UseCaseGroup.Builder()
                useCases.forEach { builder.addUseCase(it) }
                if (wantsEffect && useCases.size > 1) {
                    builder.addEffect(pipeline.acquireEffect())
                }
                camera = cameraProvider.bindToLifecycle(lifecycleOwner, selector, builder.build())
                boundUseCases = useCases
                effectAttached = wantsEffect && useCases.size > 1
                if (!effectAttached) pipeline.release()
                zoomController.attach(camera)
                zoomController.reapply()
                val selectionChanged = refineVideoAvailability()
                viewModel.mutate {
                    it.copy(
                        cameraReady = true,
                        cameraMinZoom = zoomController.minRatio,
                        cameraMaxZoom = zoomController.maxCameraRatio,
                        hasFlashUnit = camera?.cameraInfo?.hasFlashUnit() == true,
                    )
                }
                // The refinement can move the selection off an unsupported quality,
                // which means the bound recorder no longer matches the selection.
                if (selectionChanged) rebind()
                return
            } catch (e: IllegalArgumentException) {
                Log.w(TAG, "Vinculación rechazada, se prueba una resolución menor", e)
                // Only the still resolution has alternatives to fall back to.
                if (viewModel.current.mode != CaptureMode.PHOTO) break
                if (!degradePhotoResolution()) break
                imageCapture = null
                attempt++
            } catch (e: IllegalStateException) {
                Log.e(TAG, "Estado inválido al vincular", e)
                break
            }
        }
        pipeline.release()
        effectAttached = false
        viewModel.mutate { it.copy(cameraReady = false) }
        viewModel.notify(UiNotice.Resource(R.string.error_bind_failed))
    }

    /**
     * Removes only what the new configuration no longer needs. Adding or
     * removing the effect always needs a fresh group, so that case unbinds all.
     */
    private fun unbindStale(
        cameraProvider: ProcessCameraProvider,
        desired: List<UseCase>,
        wantsEffect: Boolean,
    ) {
        if (wantsEffect != effectAttached || boundUseCases.isEmpty()) {
            cameraProvider.unbindAll()
            boundUseCases = emptyList()
            return
        }
        val stale = boundUseCases.filter { bound -> desired.none { it === bound } }
        if (stale.isNotEmpty()) {
            cameraProvider.unbind(*stale.toTypedArray())
            boundUseCases = boundUseCases - stale.toSet()
        }
    }

    private fun buildUseCases(cameraProvider: ProcessCameraProvider): List<UseCase> {
        val state = viewModel.current
        val previewUseCase = preview ?: buildPreview().also { preview = it }
        val useCases = mutableListOf<UseCase>(previewUseCase)
        when (state.mode) {
            CaptureMode.PHOTO -> {
                videoCapture?.let { cameraProvider.unbind(it) }
                videoCapture = null
                val capture = imageCapture
                    ?: photoController.buildImageCapture(photoTarget, state.flashMode)
                        .also { imageCapture = it }
                useCases += capture
            }

            CaptureMode.VIDEO -> {
                imageCapture?.let { cameraProvider.unbind(it) }
                imageCapture = null
                val availability = state.selectedVideoAvailability
                if (availability != null &&
                    availability.available &&
                    availability.mode.kind == RecordingKind.NORMAL
                ) {
                    val capture = videoController.buildVideoCapture(
                        availability.mode,
                        executor,
                        state.stabilizationRequested && availability.stabilizationSupported,
                    )
                    videoCapture = capture
                    useCases += capture
                } else {
                    videoCapture?.let { cameraProvider.unbind(it) }
                    videoCapture = null
                }
            }
        }
        return useCases
    }

    /**
     * Cross-checks the Camera2 probe against the qualities the CameraX recorder
     * reports for the bound camera, and disables anything it will not accept.
     */
    private fun refineVideoAvailability(): Boolean {
        if (qualitiesRefined) return false
        val info = camera?.cameraInfo ?: return false
        val supported = videoController.supportedQualities(info)
        if (supported.isEmpty()) return false
        val current = viewModel.current.videoModes
        if (current.isEmpty()) return false
        qualitiesRefined = true
        var changed = false
        val refined = current.map { availability ->
            if (!availability.available || availability.mode.kind != RecordingKind.NORMAL) {
                availability
            } else if (supported.contains(availability.mode.toQuality())) {
                availability
            } else {
                changed = true
                availability.copy(
                    available = false,
                    reason = UnavailableReason.CAMERA_SIZE_UNSUPPORTED,
                    detail = context.getString(
                        R.string.notice_quality_recheck,
                        availability.mode.label,
                    ),
                )
            }
        }
        if (!changed) return false
        val previousSelection = viewModel.current.selectedVideoMode
        viewModel.mutate { state ->
            val fallback = VideoModeCatalog.fallbackFor(state.selectedVideoMode, refined)
            state.copy(
                videoModes = refined,
                selectedVideoMode = fallback?.mode?.id ?: state.selectedVideoMode,
            )
        }
        val newSelection = viewModel.current.selectedVideoMode
        if (newSelection != previousSelection) {
            viewModel.notify(UiNotice.Resource(R.string.notice_video_mode_changed))
            horizon.setFrameSize(viewModel.current.selectedMode.size)
        }
        return newSelection != previousSelection && viewModel.current.mode == CaptureMode.VIDEO
    }

    /** Steps down the still resolution after a rejected binding. */
    private fun degradePhotoResolution(): Boolean {
        val next = ResolutionSelection.nextSmaller(jpegCandidates, photoTarget) ?: return false
        photoTarget = next
        viewModel.mutate {
            it.copy(
                photoTargetSize = next,
                photoDegraded = true,
                photoTargetIsHighResolution = false,
            )
        }
        viewModel.notify(UiNotice.Resource(R.string.notice_resolution_degraded))
        return true
    }

    private fun needsEffect(): Boolean {
        val state = viewModel.current
        if (state.mode != CaptureMode.VIDEO) return false
        if (state.selectedMode.kind == RecordingKind.HIGH_SPEED) return false
        return state.horizonMode != HorizonMode.OFF || state.digitalFactor > 1.001f
    }

    private fun buildPreview(): Preview {
        val target = viewModel.current.selectedMode.size
        val reported = viewModel.report?.mainProbe?.previewSizes.orEmpty()
        val previewSize = ResolutionSelection.previewSizeFor(target, reported)
            ?: SizeSpec(target.width.coerceAtMost(1920), target.height.coerceAtMost(1080))
        val selector = ResolutionSelector.Builder()
            .setAspectRatioStrategy(AspectRatioStrategy.RATIO_16_9_FALLBACK_AUTO_STRATEGY)
            .setResolutionStrategy(
                ResolutionStrategy(
                    Size(previewSize.width, previewSize.height),
                    ResolutionStrategy.FALLBACK_RULE_CLOSEST_LOWER_THEN_HIGHER,
                ),
            )
            .build()
        return Preview.Builder()
            .setResolutionSelector(selector)
            .build()
            .also { it.setSurfaceProvider(previewView.surfaceProvider) }
    }

    /** Frees CameraX so a Camera2 high speed session can open the device. */
    fun releaseCameraX() {
        provider?.unbindAll()
        boundUseCases = emptyList()
        camera = null
        zoomController.detach()
        pipeline.release()
        effectAttached = false
        viewModel.mutate { it.copy(cameraReady = false) }
    }

    fun shutdown() {
        releaseCameraX()
        preview = null
        imageCapture = null
        videoCapture = null
        provider = null
    }

    private companion object {
        const val TAG = "CameraCoordinator"
        const val MAX_BIND_ATTEMPTS = 4
    }
}
