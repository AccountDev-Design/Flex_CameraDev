package com.flex.cameradev.ui

import android.content.Context
import com.flex.cameradev.R
import com.flex.cameradev.core.HorizonBlockReason
import com.flex.cameradev.core.HorizonMode
import com.flex.cameradev.core.UnavailableReason
import com.flex.cameradev.core.VideoModeAvailability
import com.flex.cameradev.core.VideoModeId
import java.util.Locale

/** Maps the framework independent enums onto the localised strings. */
object UiStrings {

    fun reason(context: Context, availability: VideoModeAvailability): String {
        val base = when (availability.reason) {
            UnavailableReason.NOT_PROBED -> context.getString(R.string.reason_not_probed)
            UnavailableReason.CAMERA_SIZE_UNSUPPORTED ->
                context.getString(R.string.reason_size_unsupported)

            UnavailableReason.CAMERA_FPS_UNSUPPORTED ->
                context.getString(R.string.reason_fps_unsupported)

            UnavailableReason.ENCODER_UNSUPPORTED -> context.getString(R.string.reason_encoder)
            UnavailableReason.HIGH_SPEED_UNSUPPORTED ->
                context.getString(R.string.reason_high_speed_unsupported)

            UnavailableReason.HIGH_SPEED_VENDOR_ONLY ->
                if (availability.mode.id == VideoModeId.HD240) {
                    context.getString(R.string.high_speed_vendor_only)
                } else {
                    context.getString(R.string.high_speed_vendor_only_generic)
                }

            UnavailableReason.PROBE_FAILED -> context.getString(R.string.reason_probe_failed)
            null -> ""
        }
        val detail = availability.detail
        return if (detail.isNullOrBlank()) base else "$base ($detail)"
    }

    fun horizonLabel(context: Context, mode: HorizonMode): String = when (mode) {
        HorizonMode.OFF -> context.getString(R.string.horizon_off)
        HorizonMode.LEVELING -> context.getString(R.string.horizon_leveling)
        HorizonMode.LOCK -> context.getString(R.string.horizon_lock)
    }

    fun horizonBlockReason(context: Context, reason: HorizonBlockReason?): String? = when (reason) {
        HorizonBlockReason.HIGH_SPEED_SESSION -> context.getString(R.string.horizon_high_speed)
        HorizonBlockReason.PIPELINE_UNAVAILABLE -> context.getString(R.string.horizon_no_sensor)
        HorizonBlockReason.PERFORMANCE_BUDGET -> context.getString(R.string.horizon_performance)
        null -> null
    }

    /** mm:ss, or hh:mm:ss once the recording passes an hour. */
    fun duration(millis: Long): String {
        val totalSeconds = (millis / 1000L).coerceAtLeast(0L)
        val hours = totalSeconds / 3600
        val minutes = (totalSeconds % 3600) / 60
        val seconds = totalSeconds % 60
        return if (hours > 0) {
            String.format(Locale.US, "%d:%02d:%02d", hours, minutes, seconds)
        } else {
            String.format(Locale.US, "%02d:%02d", minutes, seconds)
        }
    }

    fun bytes(value: Long): String {
        if (value <= 0L) return "0 MB"
        val gigabytes = value / (1024.0 * 1024.0 * 1024.0)
        if (gigabytes >= 1.0) return String.format(Locale.US, "%.1f GB", gigabytes)
        val megabytes = value / (1024.0 * 1024.0)
        return String.format(Locale.US, "%.0f MB", megabytes)
    }
}
