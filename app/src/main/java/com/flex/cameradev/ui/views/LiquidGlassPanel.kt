package com.flex.cameradev.ui.views

import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.util.AttributeSet
import android.widget.FrameLayout
import androidx.core.content.ContextCompat
import com.flex.cameradev.R

/**
 * The floating panel of the interface.
 *
 * The look comes from a translucent gradient, a hairline border and a single
 * highlight stroke: no live blur of the camera feed, because that would mean
 * reading back every preview frame. [lightweight] drops the highlight and the
 * shadow while a heavy capture mode is running.
 */
class LiquidGlassPanel @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : FrameLayout(context, attrs, defStyleAttr) {

    private val bounds = RectF()

    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val borderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = ContextCompat.getColor(context, R.color.glass_border)
    }
    private val highlightPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = ContextCompat.getColor(context, R.color.glass_highlight)
    }

    private val topTint = ContextCompat.getColor(context, R.color.glass_tint)
    private val bottomTint = ContextCompat.getColor(context, R.color.glass_tint_soft)

    private val cornerRadius = resources.getDimension(R.dimen.panel_radius)

    /** Reduces the decoration while 4K, 60 fps or the horizon pipeline are active. */
    var lightweight: Boolean = false
        set(value) {
            if (field == value) return
            field = value
            invalidate()
        }

    init {
        setWillNotDraw(false)
        val strokeWidth = resources.displayMetrics.density
        borderPaint.strokeWidth = strokeWidth
        highlightPaint.strokeWidth = strokeWidth
    }

    override fun onSizeChanged(width: Int, height: Int, oldWidth: Int, oldHeight: Int) {
        super.onSizeChanged(width, height, oldWidth, oldHeight)
        val inset = borderPaint.strokeWidth / 2f
        bounds.set(inset, inset, width - inset, height - inset)
        fillPaint.shader = LinearGradient(
            0f,
            0f,
            0f,
            height.toFloat(),
            topTint,
            bottomTint,
            Shader.TileMode.CLAMP,
        )
    }

    override fun onDraw(canvas: Canvas) {
        if (bounds.isEmpty) return
        canvas.drawRoundRect(bounds, cornerRadius, cornerRadius, fillPaint)
        canvas.drawRoundRect(bounds, cornerRadius, cornerRadius, borderPaint)
        if (!lightweight) {
            // A single arc along the top edge reads as a reflection without an
            // extra layer or an offscreen buffer.
            val inset = cornerRadius / 2f
            canvas.drawLine(
                bounds.left + inset,
                bounds.top + borderPaint.strokeWidth,
                bounds.right - inset,
                bounds.top + borderPaint.strokeWidth,
                highlightPaint,
            )
        }
        super.onDraw(canvas)
    }
}
