package com.flex.cameradev.core

import kotlin.math.abs
import kotlin.math.atan
import kotlin.math.roundToInt

/**
 * Everything the app knows about one camera before deciding which one is the
 * main sensor. Filled in from CameraCharacteristics; kept framework free so the
 * selection rules can be tested.
 */
data class CameraCandidate(
    val id: String,
    val facingBack: Boolean,
    val maxJpegSize: SizeSpec,
    val hasOpticalStabilization: Boolean,
    /** SENSOR_INFO_PHYSICAL_SIZE width in millimetres, 0 when unknown. */
    val sensorWidthMm: Float,
    /** First entry of LENS_INFO_AVAILABLE_FOCAL_LENGTHS, 0 when unknown. */
    val focalLengthMm: Float,
    /** LENS_INFO_MINIMUM_FOCUS_DISTANCE, 0 means a fixed focus lens. */
    val minimumFocusDistanceDiopters: Float,
    val isLogical: Boolean,
    val physicalIds: Set<String> = emptySet(),
    val supportsVideoRecording: Boolean = true,
    val maxVideoSize: SizeSpec? = null,
) {
    val megapixels: Double get() = MegapixelMath.megapixels(maxJpegSize.width, maxJpegSize.height)

    /**
     * Horizontal field of view in degrees. Ultra wide modules land far above the
     * main sensor, macro modules sit slightly above it.
     */
    val horizontalFovDegrees: Float
        get() {
            if (sensorWidthMm <= 0f || focalLengthMm <= 0f) return 0f
            val halfAngle = atan((sensorWidthMm / (2f * focalLengthMm)).toDouble())
            return (2.0 * halfAngle * 180.0 / Math.PI).toFloat()
        }

    val hasAutoFocus: Boolean get() = minimumFocusDistanceDiopters > 0f
}

/**
 * Picks the main rear camera without relying on the id being "0".
 *
 * The resolution tier decides first, because on this class of device the main
 * sensor is the only high resolution one. Inside a tier the sensor traits
 * (stabilisation, auto focus, a normal field of view) break the tie.
 */
object CameraScoring {

    /** Field of view a conventional main camera lands on. */
    private const val TYPICAL_MAIN_FOV = 66f

    /** Above this the module is a wide angle one and must not be the default. */
    const val ULTRA_WIDE_FOV_THRESHOLD = 85f

    /** Cameras below this are helper modules (macro, depth) on this class of phone. */
    const val HELPER_MODULE_MEGAPIXELS = 8.0

    /** Groups similar resolutions so 8160x6120 and 8000x6000 land in the same tier. */
    fun resolutionTier(candidate: CameraCandidate): Int =
        candidate.megapixels.roundToInt()

    fun traitScore(candidate: CameraCandidate): Int {
        var score = 0
        if (candidate.hasOpticalStabilization) score += 5_000
        if (candidate.hasAutoFocus) score += 3_000
        if (candidate.supportsVideoRecording) score += 1_500
        if (candidate.isLogical) score += 800
        val fov = candidate.horizontalFovDegrees
        if (fov > 0f) {
            if (fov >= ULTRA_WIDE_FOV_THRESHOLD) score -= 6_000
            score -= (abs(fov - TYPICAL_MAIN_FOV) * 20f).roundToInt()
        }
        if (candidate.megapixels in 0.1..HELPER_MODULE_MEGAPIXELS) score -= 4_000
        return score
    }

    /** Full ordering used by the diagnostics screen. */
    fun rank(candidates: List<CameraCandidate>): List<CameraCandidate> =
        candidates
            .filter { it.facingBack }
            .sortedWith(
                compareByDescending<CameraCandidate> { resolutionTier(it) }
                    .thenByDescending { traitScore(it) }
                    .thenBy { it.id },
            )

    /**
     * The camera the app binds to by default. Returns null when the device
     * exposes no rear camera at all, which the caller has to report.
     */
    fun pickMain(candidates: List<CameraCandidate>): CameraCandidate? = rank(candidates).firstOrNull()

    /** True when [candidate] looks like the ultra wide module. */
    fun isUltraWide(candidate: CameraCandidate): Boolean =
        candidate.horizontalFovDegrees >= ULTRA_WIDE_FOV_THRESHOLD

    /** True when [candidate] looks like a macro or depth helper module. */
    fun isHelperModule(candidate: CameraCandidate): Boolean =
        candidate.megapixels > 0.0 && candidate.megapixels <= HELPER_MODULE_MEGAPIXELS
}
