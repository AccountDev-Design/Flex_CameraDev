package com.flex.cameradev.camera

import android.content.Context
import android.graphics.ImageFormat
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraMetadata
import android.hardware.camera2.params.StreamConfigurationMap
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.media.MediaRecorder
import android.os.Build
import android.util.Log
import android.util.Size
import com.flex.cameradev.core.CameraCandidate
import com.flex.cameradev.core.CameraScoring
import com.flex.cameradev.core.RecordingKind
import com.flex.cameradev.core.ResolutionSelection
import com.flex.cameradev.core.SizeSpec
import com.flex.cameradev.core.UnavailableReason
import com.flex.cameradev.core.VideoMode
import com.flex.cameradev.core.VideoModeAvailability
import com.flex.cameradev.core.VideoModeCatalog

/** Everything read from CameraCharacteristics for a single camera id. */
data class CameraProbe(
    val candidate: CameraCandidate,
    val jpegSizes: List<SizeSpec>,
    val highResolutionJpegSizes: List<SizeSpec>,
    val recorderSizes: List<SizeSpec>,
    val previewSizes: List<SizeSpec>,
    val aeFpsRanges: List<IntRange>,
    val highSpeedSizes: List<SizeSpec>,
    val highSpeedFpsRanges: Map<SizeSpec, List<IntRange>>,
    val hasFlash: Boolean,
    val videoStabilizationModes: List<Int>,
    val hardwareLevel: Int,
    val sensorOrientation: Int,
    val activeArray: SizeSpec?,
    val physicalSensorSizeMm: SizeMm?,
    val focalLengths: List<Float>,
    val zoomRange: ClosedFloatingPointRange<Float>?,
    val maxDigitalZoom: Float,
) {
    val id: String get() = candidate.id

    val supportsVideoStabilization: Boolean
        get() = videoStabilizationModes.contains(CameraMetadata.CONTROL_VIDEO_STABILIZATION_MODE_ON)

    val hardwareLevelLabel: String
        get() = when (hardwareLevel) {
            CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY -> "LEGACY"
            CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED -> "LIMITED"
            CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_FULL -> "FULL"
            CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_3 -> "LEVEL_3"
            CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL -> "EXTERNAL"
            else -> "DESCONOCIDO"
        }
}

data class SizeMm(val widthMm: Float, val heightMm: Float)

/** Result of probing the whole device once. */
data class DeviceCameraReport(
    val probes: List<CameraProbe>,
    val mainCameraId: String?,
    val error: String? = null,
) {
    fun probeFor(id: String?): CameraProbe? = probes.firstOrNull { it.id == id }

    val mainProbe: CameraProbe? get() = probeFor(mainCameraId)
}

/**
 * Reads the real Camera2 metadata of every camera on the device.
 *
 * Nothing here guesses: a value that the platform does not report is left out
 * of the report instead of being filled with a plausible number.
 */
object CameraCapabilities {

    private const val TAG = "CameraCapabilities"

    fun probeDevice(context: Context): DeviceCameraReport {
        val manager = context.getSystemService(Context.CAMERA_SERVICE) as? CameraManager
            ?: return DeviceCameraReport(emptyList(), null, "CameraManager no disponible")
        return try {
            val probes = manager.cameraIdList.mapNotNull { id -> probeCamera(manager, id) }
            val main = CameraScoring.pickMain(probes.map { it.candidate })
            DeviceCameraReport(probes, main?.id)
        } catch (e: CameraAccessException) {
            Log.w(TAG, "No se pudo enumerar las cámaras", e)
            DeviceCameraReport(emptyList(), null, e.message ?: "CameraAccessException")
        } catch (e: IllegalArgumentException) {
            Log.w(TAG, "Metadatos de cámara inválidos", e)
            DeviceCameraReport(emptyList(), null, e.message ?: "IllegalArgumentException")
        }
    }

    private fun probeCamera(manager: CameraManager, id: String): CameraProbe? {
        val characteristics = try {
            manager.getCameraCharacteristics(id)
        } catch (e: CameraAccessException) {
            Log.w(TAG, "Sin acceso a la cámara $id", e)
            return null
        } catch (e: IllegalArgumentException) {
            Log.w(TAG, "Cámara $id desconocida", e)
            return null
        }

        val map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
        val facing = characteristics.get(CameraCharacteristics.LENS_FACING)
        val jpeg = map?.getOutputSizes(ImageFormat.JPEG).orEmptySizes()
        val highRes = map.highResolutionJpegSizes()
        val recorder = map?.getOutputSizes(MediaRecorder::class.java).orEmptySizes()
        val preview = map?.getOutputSizes(android.graphics.SurfaceTexture::class.java).orEmptySizes()

        val oisModes = characteristics
            .get(CameraCharacteristics.LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION)
            ?.toList()
            .orEmpty()
        val hasOis = oisModes.contains(CameraMetadata.LENS_OPTICAL_STABILIZATION_MODE_ON)

        val focalLengths = characteristics
            .get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS)
            ?.toList()
            .orEmpty()

        val sensorSize = characteristics.get(CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE)
            ?.let { SizeMm(it.width, it.height) }

        val activeArray = characteristics.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE)
            ?.let { SizeSpec(it.width(), it.height()) }

        val capabilities = characteristics
            .get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)
            ?.toList()
            .orEmpty()
        val isLogical = capabilities.contains(
            CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA,
        )
        val physicalIds: Set<String> = characteristics.physicalCameraIds

        val aeRanges = characteristics
            .get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
            ?.map { it.lower..it.upper }
            .orEmpty()

        val highSpeedSizes = map?.highSpeedVideoSizes.orEmptySizes()
        val highSpeedRanges = highSpeedSizes.associateWith { size ->
            try {
                map?.getHighSpeedVideoFpsRangesFor(Size(size.width, size.height))
                    ?.map { it.lower..it.upper }
                    .orEmpty()
            } catch (e: IllegalArgumentException) {
                Log.w(TAG, "Sin rangos de alta velocidad para $size", e)
                emptyList()
            }
        }

        val zoomRange = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            characteristics.get(CameraCharacteristics.CONTROL_ZOOM_RATIO_RANGE)
                ?.let { it.lower..it.upper }
        } else {
            null
        }
        val maxDigitalZoom = characteristics
            .get(CameraCharacteristics.SCALER_AVAILABLE_MAX_DIGITAL_ZOOM) ?: 1f

        val candidate = CameraCandidate(
            id = id,
            facingBack = facing == CameraCharacteristics.LENS_FACING_BACK,
            maxJpegSize = (highRes + jpeg).maxByOrNull { it.area } ?: SizeSpec(0, 0),
            hasOpticalStabilization = hasOis,
            sensorWidthMm = sensorSize?.widthMm ?: 0f,
            focalLengthMm = focalLengths.firstOrNull() ?: 0f,
            minimumFocusDistanceDiopters =
            characteristics.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE) ?: 0f,
            isLogical = isLogical,
            physicalIds = physicalIds,
            supportsVideoRecording = recorder.isNotEmpty(),
            maxVideoSize = recorder.maxByOrNull { it.area },
        )

        return CameraProbe(
            candidate = candidate,
            jpegSizes = jpeg.sortedByDescending { it.area },
            highResolutionJpegSizes = highRes.sortedByDescending { it.area },
            recorderSizes = recorder.sortedByDescending { it.area },
            previewSizes = preview.sortedByDescending { it.area },
            aeFpsRanges = aeRanges,
            highSpeedSizes = highSpeedSizes,
            highSpeedFpsRanges = highSpeedRanges,
            hasFlash = characteristics.get(CameraCharacteristics.FLASH_INFO_AVAILABLE) ?: false,
            videoStabilizationModes = characteristics
                .get(CameraCharacteristics.CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES)
                ?.toList()
                .orEmpty(),
            hardwareLevel = characteristics
                .get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL) ?: -1,
            sensorOrientation = characteristics
                .get(CameraCharacteristics.SENSOR_ORIENTATION) ?: 0,
            activeArray = activeArray,
            physicalSensorSizeMm = sensorSize,
            focalLengths = focalLengths,
            zoomRange = zoomRange,
            maxDigitalZoom = maxDigitalZoom,
        )
    }

    private fun StreamConfigurationMap?.highResolutionJpegSizes(): List<SizeSpec> =
        try {
            this?.getHighResolutionOutputSizes(ImageFormat.JPEG).orEmptySizes()
        } catch (e: IllegalArgumentException) {
            Log.w(TAG, "Sin tamaños JPEG de alta resolución", e)
            emptyList()
        }

    private fun Array<Size>?.orEmptySizes(): List<SizeSpec> =
        this?.map { SizeSpec(it.width, it.height) }.orEmpty()
}

/** Asks the platform encoders whether a size and frame rate can actually be muxed. */
object EncoderSupport {

    private val VIDEO_MIME_TYPES = listOf(MediaFormat.MIMETYPE_VIDEO_AVC, MediaFormat.MIMETYPE_VIDEO_HEVC)

    private val codecInfos: List<MediaCodecInfo> by lazy {
        try {
            MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.filter { it.isEncoder }
        } catch (e: RuntimeException) {
            emptyList()
        }
    }

    /** Name of the encoder that covers the combination, for the diagnostics screen. */
    fun matchingEncoder(size: SizeSpec, fps: Int): String? {
        if (size.width <= 0 || size.height <= 0 || fps <= 0) return null
        for (info in codecInfos) {
            for (mime in info.supportedTypes) {
                if (mime !in VIDEO_MIME_TYPES) continue
                val caps = try {
                    info.getCapabilitiesForType(mime).videoCapabilities
                } catch (e: IllegalArgumentException) {
                    null
                } ?: continue
                val ok = try {
                    caps.areSizeAndRateSupported(size.width, size.height, fps.toDouble())
                } catch (e: IllegalArgumentException) {
                    false
                }
                if (ok) return info.name
            }
        }
        return null
    }
}

/**
 * Turns the raw probe into the availability list the quality panel renders.
 *
 * A mode is only enabled when the camera reports the size, the camera reports
 * the frame rate and a platform encoder accepts the pair.
 */
object VideoModeProbe {

    fun evaluate(probe: CameraProbe?): List<VideoModeAvailability> {
        if (probe == null) {
            return VideoModeCatalog.ALL.map {
                VideoModeAvailability(it, available = false, reason = UnavailableReason.NOT_PROBED)
            }
        }
        return VideoModeCatalog.ALL.map { mode ->
            when (mode.kind) {
                RecordingKind.NORMAL -> evaluateNormal(probe, mode)
                RecordingKind.HIGH_SPEED -> evaluateHighSpeed(probe, mode)
            }
        }
    }

    private fun evaluateNormal(probe: CameraProbe, mode: VideoMode): VideoModeAvailability {
        val sizeSupported = probe.recorderSizes.any { it == mode.size }
        if (!sizeSupported) {
            return VideoModeAvailability(
                mode = mode,
                available = false,
                reason = UnavailableReason.CAMERA_SIZE_UNSUPPORTED,
                detail = "MediaRecorder: " + probe.recorderSizes.take(6).joinToString(),
            )
        }
        val fpsRange = ResolutionSelection.pickFpsRange(mode.fps, probe.aeFpsRanges)
        if (fpsRange == null) {
            return VideoModeAvailability(
                mode = mode,
                available = false,
                reason = UnavailableReason.CAMERA_FPS_UNSUPPORTED,
                detail = "Rangos AE: " + probe.aeFpsRanges.joinToString { "${it.first}-${it.last}" },
            )
        }
        val fixedRate = ResolutionSelection.supportsFixedFps(mode.fps, probe.aeFpsRanges)
        val encoder = EncoderSupport.matchingEncoder(mode.size, mode.fps)
        if (encoder == null) {
            return VideoModeAvailability(
                mode = mode,
                available = false,
                reason = UnavailableReason.ENCODER_UNSUPPORTED,
                detail = "${mode.size} @ ${mode.fps}",
            )
        }
        return VideoModeAvailability(
            mode = mode,
            available = true,
            detail = if (fixedRate) {
                "$encoder · ${mode.fps} fps"
            } else {
                "$encoder · ${fpsRange.first}-${fpsRange.last}"
            },
            audioSupported = true,
            stabilizationSupported = probe.supportsVideoStabilization && mode.fps <= 60,
        )
    }

    private fun evaluateHighSpeed(probe: CameraProbe, mode: VideoMode): VideoModeAvailability {
        if (probe.highSpeedSizes.isEmpty()) {
            return VideoModeAvailability(
                mode = mode,
                available = false,
                reason = UnavailableReason.HIGH_SPEED_VENDOR_ONLY,
                detail = null,
                audioSupported = false,
            )
        }
        val sizeMatch = probe.highSpeedSizes.firstOrNull { it == mode.size }
            ?: return VideoModeAvailability(
                mode = mode,
                available = false,
                reason = UnavailableReason.CAMERA_SIZE_UNSUPPORTED,
                detail = probe.highSpeedSizes.joinToString(),
                audioSupported = false,
            )
        val ranges = probe.highSpeedFpsRanges[sizeMatch].orEmpty()
        val rateSupported = ranges.any { it.last == mode.fps }
        if (!rateSupported) {
            return VideoModeAvailability(
                mode = mode,
                available = false,
                reason = UnavailableReason.HIGH_SPEED_VENDOR_ONLY,
                detail = ranges.joinToString { "${it.first}-${it.last}" },
                audioSupported = false,
            )
        }
        val encoder = EncoderSupport.matchingEncoder(mode.size, mode.fps)
        if (encoder == null) {
            return VideoModeAvailability(
                mode = mode,
                available = false,
                reason = UnavailableReason.ENCODER_UNSUPPORTED,
                detail = "${mode.size} @ ${mode.fps}",
                audioSupported = false,
            )
        }
        return VideoModeAvailability(
            mode = mode,
            available = true,
            detail = encoder,
            audioSupported = false,
            stabilizationSupported = false,
        )
    }
}
