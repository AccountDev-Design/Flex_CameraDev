package com.flex.cameradev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MegapixelMathTest {

    @Test
    fun fullSensorCaptureIsDescribedExactly() {
        assertEquals("8160 × 6120 · 49.9 MP", MegapixelMath.describe(8160, 6120))
    }

    @Test
    fun binnedCaptureIsDescribedExactly() {
        assertEquals("4080 × 3060 · 12.5 MP", MegapixelMath.describe(4080, 3060))
    }

    @Test
    fun megapixelsUseTheDecimalPointRegardlessOfLocale() {
        val previous = java.util.Locale.getDefault()
        try {
            java.util.Locale.setDefault(java.util.Locale.GERMANY)
            assertEquals("49.9", MegapixelMath.formatMegapixels(MegapixelMath.megapixels(8160, 6120)))
        } finally {
            java.util.Locale.setDefault(previous)
        }
    }

    @Test
    fun invalidSizesFallBackToAPlaceholder() {
        assertEquals("—", MegapixelMath.describe(0, 6120))
        assertEquals("—", MegapixelMath.describe(8160, -1))
        assertEquals(0.0, MegapixelMath.megapixels(-4, 4), 0.0)
    }

    @Test
    fun aBinnedFileAgainstAFullSensorClaimIsFlaggedAsVendorLimited() {
        assertTrue(
            MegapixelMath.isVendorLimited(
                actual = SizeSpec(4080, 3060),
                advertised = SizeSpec(8160, 6120),
            ),
        )
    }

    @Test
    fun aFullSizeFileIsNotFlagged() {
        assertFalse(
            MegapixelMath.isVendorLimited(
                actual = SizeSpec(8160, 6120),
                advertised = SizeSpec(8160, 6120),
            ),
        )
        // A small rounding difference must not trigger the vendor warning.
        assertFalse(
            MegapixelMath.isVendorLimited(
                actual = SizeSpec(8000, 6000),
                advertised = SizeSpec(8160, 6120),
            ),
        )
    }

    @Test
    fun unknownSizesAreNeverReportedAsLimited() {
        assertFalse(MegapixelMath.isVendorLimited(SizeSpec(0, 0), SizeSpec(8160, 6120)))
        assertFalse(MegapixelMath.isVendorLimited(SizeSpec(4080, 3060), SizeSpec(0, 0)))
    }

    @Test
    fun roundedMegapixelsDriveTheHeadlineChip() {
        assertEquals(50, MegapixelMath.roundedMegapixels(8160, 6120))
        assertEquals(12, MegapixelMath.roundedMegapixels(4080, 3060))
    }
}
