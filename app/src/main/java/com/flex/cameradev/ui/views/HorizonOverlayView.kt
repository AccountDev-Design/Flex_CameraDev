package com.flex.cameradev.ui.views

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.util.AttributeSet
import android.view.View
import androidx.core.content.ContextCompat
import com.flex.cameradev.R
import com.flex.cameradev.core.HorizonCorrection
import com.flex.cameradev.core.HorizonMath
import com.flex.cameradev.core.HorizonMode
import java.util.Locale
import kotlin.math.abs

/**
 * The horizon guide.
 *
 * It is a plain overlay view, so it is never part of the recorded frames: the
 * recording sees only the GPU pipeline output.
 */
class HorizonOverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : View(context, attrs, defStyleAttr) {

    private val density = resources.displayMetrics.density

    private val levelColor = ContextCompat.getColor(context, R.color.level_green)
    private val correctingColor = ContextCompat.getColor(context, R.color.accent_cyan)
    private val limitColor = ContextCompat.getColor(context, R.color.warning_amber)

    private val guidePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2f * density
        strokeCap = Paint.Cap.ROUND
    }
    private val referencePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1f * density
        color = 0x55FFFFFF
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = ContextCompat.getColor(context, R.color.text_primary)
        textSize = 12f * density
        textAlign = Paint.Align.CENTER
    }

    private var displayedRoll = 0f
    private var correction: HorizonCorrection? = null
    private var mode: HorizonMode = HorizonMode.OFF

    init {
        isClickable = false
        isFocusable = false
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    /**
     * Feeds one sensor update. The value is interpolated towards the target so
     * the line glides instead of snapping, and the wrap-around is handled by
     * [HorizonMath.lerpAngle].
     */
    fun update(measuredRoll: Float, correction: HorizonCorrection?, mode: HorizonMode) {
        this.correction = correction
        this.mode = mode
        displayedRoll = HorizonMath.lerpAngle(displayedRoll, measuredRoll, SMOOTHING)
        invalidate()
    }

    /** Status label the panel shows next to the guide. */
    fun statusTextRes(): Int {
        val current = correction
        return when {
            current != null && current.atLimit -> R.string.horizon_status_limit
            HorizonMath.isLevel(displayedRoll, LEVEL_TOLERANCE) -> R.string.horizon_status_level
            else -> R.string.horizon_status_correcting
        }
    }

    fun angleText(): String =
        String.format(Locale.US, "%.1f", abs(displayedRoll))

    override fun onDraw(canvas: Canvas) {
        if (width == 0 || height == 0) return
        val centerX = width / 2f
        val centerY = height / 2f
        val halfLength = width * 0.30f
        val gap = width * 0.06f

        // Fixed reference marks that show where level is.
        canvas.drawLine(
            centerX - halfLength,
            centerY,
            centerX - halfLength + gap,
            centerY,
            referencePaint,
        )
        canvas.drawLine(
            centerX + halfLength - gap,
            centerY,
            centerX + halfLength,
            centerY,
            referencePaint,
        )

        val current = correction
        guidePaint.color = when {
            current != null && current.atLimit -> limitColor
            HorizonMath.isLevel(displayedRoll, LEVEL_TOLERANCE) -> levelColor
            else -> correctingColor
        }

        canvas.save()
        canvas.rotate(-displayedRoll, centerX, centerY)
        canvas.drawLine(centerX - halfLength + gap, centerY, centerX - gap, centerY, guidePaint)
        canvas.drawLine(centerX + gap, centerY, centerX + halfLength - gap, centerY, guidePaint)
        canvas.drawCircle(centerX, centerY, 3f * density, guidePaint)
        canvas.restore()

        if (!HorizonMath.isLevel(displayedRoll, LEVEL_TOLERANCE)) {
            canvas.drawText(
                String.format(Locale.US, "%.1f°", abs(displayedRoll)),
                centerX,
                centerY - 16f * density,
                textPaint,
            )
        }

        if (mode != HorizonMode.OFF && current != null && current.croppedPercent > 0.5f) {
            canvas.drawText(
                String.format(Locale.US, "%.0f%%", current.croppedPercent),
                centerX,
                centerY + 26f * density,
                textPaint,
            )
        }
    }

    private companion object {
        /** Per-frame interpolation towards the sensor value. */
        const val SMOOTHING = 0.35f
        const val LEVEL_TOLERANCE = 0.8f
    }
}
