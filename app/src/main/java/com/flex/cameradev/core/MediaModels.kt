package com.flex.cameradev.core

/** Framework-independent size, so the resolution rules can be unit tested. */
data class SizeSpec(val width: Int, val height: Int) : Comparable<SizeSpec> {

    val area: Long get() = width.toLong() * height.toLong()

    val aspectRatio: Double get() = if (height <= 0) 0.0 else width.toDouble() / height.toDouble()

    override fun compareTo(other: SizeSpec): Int = area.compareTo(other.area)

    override fun toString(): String = "${width}×${height}"
}

/** How a video mode has to be driven by the camera stack. */
enum class RecordingKind {
    /** CameraX Recorder / VideoCapture session. */
    NORMAL,

    /** Camera2 constrained high speed session driving MediaRecorder. */
    HIGH_SPEED,
}

enum class VideoModeId { UHD30, FHD30, FHD60, HD30, HD60, HD120, HD240 }

/**
 * One requested resolution/frame-rate combination. Being present in the catalog
 * only means the app asks about it; availability is probed at runtime.
 */
data class VideoMode(
    val id: VideoModeId,
    val size: SizeSpec,
    val fps: Int,
    val kind: RecordingKind,
    val label: String,
    val fileTag: String,
)

/** Why a requested mode could not be enabled. Mapped to strings in the UI layer. */
enum class UnavailableReason {
    NOT_PROBED,
    CAMERA_SIZE_UNSUPPORTED,
    CAMERA_FPS_UNSUPPORTED,
    ENCODER_UNSUPPORTED,
    HIGH_SPEED_UNSUPPORTED,
    HIGH_SPEED_VENDOR_ONLY,
    PROBE_FAILED,
}

data class VideoModeAvailability(
    val mode: VideoMode,
    val available: Boolean,
    val reason: UnavailableReason? = null,
    /** Short factual detail from the probe, e.g. the FPS ranges the camera reports. */
    val detail: String? = null,
    /** True when the camera reports the mode but audio cannot be muxed with it. */
    val audioSupported: Boolean = true,
    /** True when the camera reports video stabilization for this combination. */
    val stabilizationSupported: Boolean = false,
) {
    val isHighSpeed: Boolean get() = mode.kind == RecordingKind.HIGH_SPEED
}

/** The two ways a high speed clip can be written to disk. */
enum class HighSpeedPlayback {
    /** Same duration as reality, high frame rate file. */
    REAL_TIME,

    /** Slowed down at capture time by lowering the playback frame rate. */
    SLOW_MOTION,
}

/** Every combination the app asks the device about. */
object VideoModeCatalog {

    val UHD30 = VideoMode(VideoModeId.UHD30, SizeSpec(3840, 2160), 30, RecordingKind.NORMAL, "4K · 30", "4K30")
    val FHD30 = VideoMode(VideoModeId.FHD30, SizeSpec(1920, 1080), 30, RecordingKind.NORMAL, "FHD · 30", "FHD30")
    val FHD60 = VideoMode(VideoModeId.FHD60, SizeSpec(1920, 1080), 60, RecordingKind.NORMAL, "FHD · 60", "FHD60")
    val HD30 = VideoMode(VideoModeId.HD30, SizeSpec(1280, 720), 30, RecordingKind.NORMAL, "HD · 30", "HD30")
    val HD60 = VideoMode(VideoModeId.HD60, SizeSpec(1280, 720), 60, RecordingKind.NORMAL, "HD · 60", "HD60")
    val HD120 = VideoMode(VideoModeId.HD120, SizeSpec(1280, 720), 120, RecordingKind.HIGH_SPEED, "HD · 120", "HD120")
    val HD240 = VideoMode(VideoModeId.HD240, SizeSpec(1280, 720), 240, RecordingKind.HIGH_SPEED, "HD · 240", "HD240")

    val ALL: List<VideoMode> = listOf(UHD30, FHD30, FHD60, HD30, HD60, HD120, HD240)

    fun byId(id: VideoModeId): VideoMode = ALL.first { it.id == id }

    /**
     * Picks the mode that should be selected when [preferred] is not available.
     * Falls back down the list of normal modes, never to a high speed mode.
     */
    fun fallbackFor(
        preferred: VideoModeId,
        availability: List<VideoModeAvailability>,
    ): VideoModeAvailability? {
        val byId = availability.associateBy { it.mode.id }
        byId[preferred]?.takeIf { it.available }?.let { return it }
        val order = listOf(
            VideoModeId.FHD30,
            VideoModeId.HD30,
            VideoModeId.FHD60,
            VideoModeId.HD60,
            VideoModeId.UHD30,
        )
        return order.firstNotNullOfOrNull { id -> byId[id]?.takeIf { it.available } }
            ?: availability.firstOrNull { it.available && !it.isHighSpeed }
    }
}
