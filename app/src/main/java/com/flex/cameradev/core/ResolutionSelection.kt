package com.flex.cameradev.core

import kotlin.math.abs

/** Ordering rules for still and preview resolutions. */
object ResolutionSelection {

    private const val RATIO_4_3 = 4.0 / 3.0

    /**
     * Merges the standard JPEG sizes with the high resolution ones and orders
     * them from largest to smallest. Ties prefer the 4:3 native sensor ratio,
     * which is what the main sensor delivers at full resolution.
     */
    fun orderedJpegCandidates(
        standardSizes: List<SizeSpec>,
        highResolutionSizes: List<SizeSpec> = emptyList(),
    ): List<SizeSpec> {
        val merged = LinkedHashSet<SizeSpec>()
        merged.addAll(highResolutionSizes)
        merged.addAll(standardSizes)
        return merged
            .filter { it.width > 0 && it.height > 0 }
            .sortedWith(
                compareByDescending<SizeSpec> { it.area }
                    .thenBy { abs(it.aspectRatio - RATIO_4_3) }
                    .thenByDescending { it.width },
            )
    }

    /** Largest still size the app should ask for, or null when nothing was reported. */
    fun bestJpeg(
        standardSizes: List<SizeSpec>,
        highResolutionSizes: List<SizeSpec> = emptyList(),
    ): SizeSpec? = orderedJpegCandidates(standardSizes, highResolutionSizes).firstOrNull()

    /**
     * Next size to try after a bind failure. Returns null once the list is
     * exhausted so the caller can stop degrading and report the failure.
     */
    fun nextSmaller(candidates: List<SizeSpec>, current: SizeSpec?): SizeSpec? {
        if (candidates.isEmpty()) return null
        if (current == null) return candidates.first()
        val index = candidates.indexOfFirst { it == current }
        if (index < 0) return candidates.firstOrNull { it.area < current.area }
        return candidates.getOrNull(index + 1)
    }

    /**
     * Preview size matched to the capture aspect ratio, bounded by [maxArea] so
     * the preview stream never becomes the bottleneck.
     */
    fun previewSizeFor(
        captureSize: SizeSpec?,
        availablePreviewSizes: List<SizeSpec>,
        maxArea: Long = 1920L * 1080L,
    ): SizeSpec? {
        if (availablePreviewSizes.isEmpty()) return null
        val targetRatio = captureSize?.aspectRatio?.takeIf { it > 0.0 } ?: RATIO_4_3
        return availablePreviewSizes
            .filter { it.width > 0 && it.height > 0 && it.area <= maxArea }
            .minByOrNull { candidate ->
                val ratioPenalty = abs(candidate.aspectRatio - targetRatio) * 10_000
                val areaPenalty = (maxArea - candidate.area).toDouble() / maxArea
                ratioPenalty + areaPenalty
            }
            ?: availablePreviewSizes.minByOrNull { it.area }
    }

    /**
     * Chooses the frame rate to request from a list the camera reports.
     * Prefers an exact fixed range, then any range whose upper bound matches.
     */
    fun pickFpsRange(target: Int, ranges: List<IntRange>): IntRange? {
        if (target <= 0 || ranges.isEmpty()) return null
        ranges.firstOrNull { it.first == target && it.last == target }?.let { return it }
        ranges.filter { it.last == target }.minByOrNull { it.last - it.first }?.let { return it }
        return ranges.filter { target in it.first..it.last }.minByOrNull { it.last - it.first }
    }

    /** True when the camera can hold [target] fps steady rather than only peak at it. */
    fun supportsFixedFps(target: Int, ranges: List<IntRange>): Boolean =
        ranges.any { it.first == target && it.last == target }
}
