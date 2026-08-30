package com.flex.cameradev.core

import java.util.Locale
import kotlin.math.roundToInt

/** Megapixel arithmetic and the exact strings shown after a capture. */
object MegapixelMath {

    /** Fraction of the advertised resolution below which we report a vendor limit. */
    private const val LIMIT_THRESHOLD = 0.6

    fun megapixels(width: Int, height: Int): Double {
        if (width <= 0 || height <= 0) return 0.0
        return width.toDouble() * height.toDouble() / 1_000_000.0
    }

    fun formatMegapixels(megapixels: Double): String =
        String.format(Locale.US, "%.1f", megapixels)

    /** Renders "8160 x 6120 . 49.9 MP" using the typographic separators of the UI. */
    fun describe(width: Int, height: Int): String {
        if (width <= 0 || height <= 0) return "—"
        return "$width × $height · ${formatMegapixels(megapixels(width, height))} MP"
    }

    /** Short form used in compact chips: "49.9 MP". */
    fun shortDescribe(width: Int, height: Int): String {
        if (width <= 0 || height <= 0) return "—"
        return "${formatMegapixels(megapixels(width, height))} MP"
    }

    /**
     * True when the file that was actually written is clearly smaller than the
     * resolution the camera advertises, which is what happens when the vendor
     * refuses full-sensor output to third-party apps.
     */
    fun isVendorLimited(actual: SizeSpec, advertised: SizeSpec): Boolean {
        if (actual.area <= 0L || advertised.area <= 0L) return false
        return actual.area.toDouble() < advertised.area.toDouble() * LIMIT_THRESHOLD
    }

    /** Rounds to whole megapixels for headline chips ("50 MP" style labels). */
    fun roundedMegapixels(width: Int, height: Int): Int = megapixels(width, height).roundToInt()
}
