package com.flex.cameradev.camera

import android.content.Context
import android.os.Build
import com.flex.cameradev.R
import com.flex.cameradev.core.CameraScoring
import com.flex.cameradev.core.MegapixelMath
import com.flex.cameradev.core.SizeSpec
import com.flex.cameradev.horizon.RollSource
import java.util.Locale

/**
 * Formats the probe results as plain text.
 *
 * Only values the platform actually reported are printed; anything missing is
 * shown as "sin datos" rather than being filled in with a plausible number.
 */
object DiagnosticsReport {

    fun build(
        context: Context,
        report: DeviceCameraReport,
        rollSource: RollSource,
    ): String = buildString {
        appendLine("${Build.MANUFACTURER} ${Build.MODEL} · Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})")
        appendLine()

        if (report.error != null) {
            appendLine("Error: ${report.error}")
            appendLine()
        }

        val main = report.mainProbe
        appendLine(context.getString(R.string.diagnostics_main_camera))
        if (main == null) {
            appendLine("  ${context.getString(R.string.diagnostics_none)}")
        } else {
            appendLine("  ${context.getString(R.string.diagnostics_logical_id)}: ${main.id}")
            appendLine("  ${context.getString(R.string.diagnostics_hardware_level)}: ${main.hardwareLevelLabel}")
            appendLine("  ${context.getString(R.string.diagnostics_max_jpeg)}: ${describe(main.candidate.maxJpegSize)}")
            appendLine(
                "  ${context.getString(R.string.diagnostics_high_res)}: " +
                    sizes(main.highResolutionJpegSizes, context),
            )
            appendLine(
                "  ${context.getString(R.string.diagnostics_video_sizes)}: " +
                    sizes(main.recorderSizes.take(8), context),
            )
            appendLine(
                "  ${context.getString(R.string.diagnostics_fps)}: " +
                    ranges(main.aeFpsRanges, context),
            )
            appendLine("  ${context.getString(R.string.diagnostics_high_speed)}:")
            if (main.highSpeedSizes.isEmpty()) {
                appendLine("    ${context.getString(R.string.diagnostics_none)}")
            } else {
                for (size in main.highSpeedSizes) {
                    appendLine("    $size -> " + ranges(main.highSpeedFpsRanges[size].orEmpty(), context))
                }
            }
            appendLine(
                "  ${context.getString(R.string.diagnostics_zoom)}: " +
                    (
                        main.zoomRange?.let {
                            String.format(Locale.US, "%.2f× – %.2f×", it.start, it.endInclusive)
                        } ?: String.format(Locale.US, "1.00× – %.2f× (digital)", main.maxDigitalZoom)
                        ),
            )
            appendLine("  ${context.getString(R.string.diagnostics_flash)}: ${yesNo(context, main.hasFlash)}")
            appendLine(
                "  ${context.getString(R.string.diagnostics_ois)}: " +
                    yesNo(context, main.candidate.hasOpticalStabilization),
            )
            appendLine(
                "  ${context.getString(R.string.diagnostics_video_stabilization)}: " +
                    yesNo(context, main.supportsVideoStabilization),
            )
            appendLine(
                "  ${context.getString(R.string.diagnostics_physical)}: " +
                    main.candidate.physicalIds.ifEmpty { setOf(context.getString(R.string.diagnostics_none)) }
                        .joinToString(),
            )
            appendLine(
                "  ${context.getString(R.string.diagnostics_sensor)}: " +
                    (
                        main.physicalSensorSizeMm?.let {
                            String.format(Locale.US, "%.2f × %.2f mm", it.widthMm, it.heightMm)
                        } ?: context.getString(R.string.diagnostics_none)
                        ) +
                    ", f=" + main.focalLengths.joinToString { String.format(Locale.US, "%.2f mm", it) } +
                    ", FOV≈" + String.format(Locale.US, "%.1f°", main.candidate.horizontalFovDegrees) +
                    ", orientación " + main.sensorOrientation + "°",
            )
        }

        appendLine()
        appendLine("${context.getString(R.string.diagnostics_imu)}: ${rollSource.name}")

        appendLine()
        appendLine(context.getString(R.string.diagnostics_modes))
        for (availability in VideoModeProbe.evaluate(main)) {
            val status = if (availability.available) "OK" else "NO"
            appendLine("  [$status] ${availability.mode.label} — ${availability.detail ?: "-"}")
        }

        appendLine()
        appendLine("Cámaras detectadas:")
        for (probe in report.probes) {
            val role = when {
                !probe.candidate.facingBack -> "frontal"
                CameraScoring.isUltraWide(probe.candidate) -> "gran angular"
                CameraScoring.isHelperModule(probe.candidate) -> "auxiliar"
                else -> "trasera"
            }
            appendLine(
                "  id=${probe.id} ($role) ${describe(probe.candidate.maxJpegSize)}" +
                    " OIS=${yesNo(context, probe.candidate.hasOpticalStabilization)}" +
                    " lógica=${yesNo(context, probe.candidate.isLogical)}",
            )
        }
    }

    private fun describe(size: SizeSpec): String =
        if (size.area <= 0L) "sin datos" else MegapixelMath.describe(size.width, size.height)

    private fun sizes(values: List<SizeSpec>, context: Context): String =
        if (values.isEmpty()) context.getString(R.string.diagnostics_none) else values.joinToString()

    private fun ranges(values: List<IntRange>, context: Context): String =
        if (values.isEmpty()) {
            context.getString(R.string.diagnostics_none)
        } else {
            values.joinToString { "${it.first}-${it.last}" }
        }

    private fun yesNo(context: Context, value: Boolean): String =
        context.getString(if (value) R.string.diagnostics_yes else R.string.diagnostics_no)
}
