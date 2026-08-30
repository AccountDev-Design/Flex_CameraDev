package com.flex.cameradev.ui

import android.net.Uri
import com.flex.cameradev.core.HighSpeedPlayback
import com.flex.cameradev.core.HorizonCompatibility
import com.flex.cameradev.core.HorizonMode
import com.flex.cameradev.core.HorizonSupport
import com.flex.cameradev.core.SizeSpec
import com.flex.cameradev.core.VideoModeAvailability
import com.flex.cameradev.core.VideoModeCatalog
import com.flex.cameradev.core.VideoModeId
import com.flex.cameradev.core.ZoomMath

enum class CaptureMode { PHOTO, VIDEO }

enum class FlashMode { AUTO, ON, OFF }

/** Photo pipeline state; the shutter is only enabled while [IDLE]. */
enum class CaptureStatus { IDLE, CAPTURING, PROCESSING, SAVING }

enum class RecordingStatus { IDLE, STARTING, RECORDING, PAUSED, STOPPING }

/** Where the zoom currently comes from, shown as a label next to the ratio. */
enum class ZoomSource { CAMERA, DIGITAL }

/**
 * The single source of truth for everything the interface renders.
 *
 * Controllers never draw; they push a new copy of this state and the activity
 * renders it, which keeps the controls, CameraX and the labels from drifting apart.
 */
data class CameraUiState(
    val mode: CaptureMode = CaptureMode.PHOTO,
    val cameraReady: Boolean = false,
    val busySwitchingMode: Boolean = false,
    val permissionCameraGranted: Boolean = false,

    // Zoom
    val requestedZoom: Float = 1f,
    val cameraZoom: Float = 1f,
    val digitalFactor: Float = 1f,
    val cameraMinZoom: Float = 1f,
    val cameraMaxZoom: Float = 1f,

    // Photo
    val photoTargetSize: SizeSpec? = null,
    val photoTargetIsHighResolution: Boolean = false,
    val photoDegraded: Boolean = false,
    val lastPhotoSize: SizeSpec? = null,
    val lastPhotoVendorLimited: Boolean = false,
    val captureStatus: CaptureStatus = CaptureStatus.IDLE,
    val saveOriginalToo: Boolean = false,

    // Flash and torch
    val hasFlashUnit: Boolean = false,
    val flashMode: FlashMode = FlashMode.AUTO,
    val torchOn: Boolean = false,

    // Audio
    val audioRequested: Boolean = true,
    val audioPermissionGranted: Boolean = false,
    val audioPermanentlyDenied: Boolean = false,

    // Video
    val videoModes: List<VideoModeAvailability> = emptyList(),
    val selectedVideoMode: VideoModeId = VideoModeId.FHD30,
    val highSpeedPlayback: HighSpeedPlayback = HighSpeedPlayback.SLOW_MOTION,
    val recordingStatus: RecordingStatus = RecordingStatus.IDLE,
    val recordedMillis: Long = 0L,
    val recordedBytes: Long = 0L,
    val freeBytes: Long = 0L,
    val stabilizationRequested: Boolean = true,
    val stabilizationSupported: Boolean = false,

    // Horizon
    val horizonMode: HorizonMode = HorizonMode.OFF,
    val horizonSupport: HorizonSupport = HorizonCompatibility.photoSupport(),
    val horizonAvailableOnDevice: Boolean = false,
    val horizonGuideVisible: Boolean = true,

    // Misc
    val gridVisible: Boolean = false,
    val lastMediaUri: Uri? = null,
    val lastMediaIsVideo: Boolean = false,
) {
    val selectedVideoAvailability: VideoModeAvailability?
        get() = videoModes.firstOrNull { it.mode.id == selectedVideoMode }

    val selectedMode get() = selectedVideoAvailability?.mode ?: VideoModeCatalog.byId(selectedVideoMode)

    val zoomSource: ZoomSource
        get() = if (digitalFactor > 1.001f) ZoomSource.DIGITAL else ZoomSource.CAMERA

    val isRecording: Boolean
        get() = recordingStatus == RecordingStatus.RECORDING || recordingStatus == RecordingStatus.PAUSED

    /** The shutter and the mode selector are locked while media is being written. */
    val isBusy: Boolean
        get() = captureStatus != CaptureStatus.IDLE ||
            recordingStatus != RecordingStatus.IDLE ||
            busySwitchingMode

    val canSwitchMode: Boolean
        get() = cameraReady && !isBusy

    val maxSelectableZoom: Float
        get() = if (mode == CaptureMode.VIDEO && selectedMode.kind == com.flex.cameradev.core.RecordingKind.HIGH_SPEED) {
            cameraMaxZoom
        } else {
            ZoomMath.MAX_DIGITAL_RATIO
        }
}

/** One-shot messages: banners, errors and confirmations. */
sealed interface UiNotice {
    data class Resource(val stringRes: Int, val formatArgs: List<Any> = emptyList()) : UiNotice
    data class Text(val message: String) : UiNotice
    data class SavedPhoto(val description: String, val vendorLimited: Boolean) : UiNotice
}
