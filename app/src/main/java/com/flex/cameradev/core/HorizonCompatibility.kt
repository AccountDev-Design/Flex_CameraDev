package com.flex.cameradev.core

/** The three horizon behaviours offered in the UI. */
enum class HorizonMode {
    /** No sensor correction at all. */
    OFF,

    /** Small tilts are corrected with a moderate crop. */
    LEVELING,

    /** Larger corrections, more crop, marked as experimental. */
    LOCK,
}

/** Why a horizon mode is not offered for the current video mode. */
enum class HorizonBlockReason {
    HIGH_SPEED_SESSION,
    PIPELINE_UNAVAILABLE,
    PERFORMANCE_BUDGET,
}

data class HorizonSupport(
    val levelingAvailable: Boolean,
    val lockAvailable: Boolean,
    val experimental: Boolean,
    val blockReason: HorizonBlockReason? = null,
    /** Magnification the pipeline may spend on this mode. */
    val maxScale: Float = 1.0f,
) {
    fun allows(mode: HorizonMode): Boolean = when (mode) {
        HorizonMode.OFF -> true
        HorizonMode.LEVELING -> levelingAvailable
        HorizonMode.LOCK -> lockAvailable
    }

    /** Nearest mode that is actually available, walking down from [requested]. */
    fun resolve(requested: HorizonMode): HorizonMode = when {
        allows(requested) -> requested
        requested == HorizonMode.LOCK && levelingAvailable -> HorizonMode.LEVELING
        else -> HorizonMode.OFF
    }
}

/**
 * Decides which horizon behaviours a video mode can sustain.
 *
 * The GPU pipeline runs on every frame, so the budget shrinks as the resolution
 * and frame rate grow, and it disappears entirely for the constrained high speed
 * sessions, which cannot carry a CameraX effect at all.
 */
object HorizonCompatibility {

    /** Crop budget for the moderate levelling mode. */
    const val LEVELING_MAX_SCALE = 1.18f

    /** Crop budget for the experimental lock. */
    const val LOCK_MAX_SCALE = 1.60f

    fun supportFor(mode: VideoMode, pipelineAvailable: Boolean): HorizonSupport {
        if (!pipelineAvailable) {
            return HorizonSupport(
                levelingAvailable = false,
                lockAvailable = false,
                experimental = false,
                blockReason = HorizonBlockReason.PIPELINE_UNAVAILABLE,
            )
        }
        if (mode.kind == RecordingKind.HIGH_SPEED) {
            return HorizonSupport(
                levelingAvailable = false,
                lockAvailable = false,
                experimental = false,
                blockReason = HorizonBlockReason.HIGH_SPEED_SESSION,
            )
        }
        val pixelRate = mode.size.area * mode.fps
        val uhd30Rate = VideoModeCatalog.UHD30.size.area * VideoModeCatalog.UHD30.fps
        return when {
            // 4K30 and FHD60 sit at the top of what the pipeline can carry, so the
            // lock stays available but is flagged and gets a smaller crop budget.
            pixelRate >= uhd30Rate -> HorizonSupport(
                levelingAvailable = true,
                lockAvailable = true,
                experimental = true,
                blockReason = HorizonBlockReason.PERFORMANCE_BUDGET,
                maxScale = 1.35f,
            )

            mode.fps >= 60 -> HorizonSupport(
                levelingAvailable = true,
                lockAvailable = true,
                experimental = true,
                maxScale = 1.45f,
            )

            else -> HorizonSupport(
                levelingAvailable = true,
                lockAvailable = true,
                experimental = false,
                maxScale = LOCK_MAX_SCALE,
            )
        }
    }

    /** Crop budget for a resolved mode pair. */
    fun maxScaleFor(support: HorizonSupport, horizonMode: HorizonMode): Float = when (horizonMode) {
        HorizonMode.OFF -> 1.0f
        HorizonMode.LEVELING -> minOf(LEVELING_MAX_SCALE, support.maxScale)
        HorizonMode.LOCK -> support.maxScale
    }

    /** Photo mode only ever levels the preview guide; capture is never rotated. */
    fun photoSupport(): HorizonSupport = HorizonSupport(
        levelingAvailable = true,
        lockAvailable = false,
        experimental = false,
        blockReason = HorizonBlockReason.PIPELINE_UNAVAILABLE,
        maxScale = 1.0f,
    )
}
