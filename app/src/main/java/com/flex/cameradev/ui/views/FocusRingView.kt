package com.flex.cameradev.ui.views

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.util.AttributeSet
import android.view.View
import androidx.core.content.ContextCompat
import com.flex.cameradev.R
import com.flex.cameradev.ui.Motion

/** Animated tap-to-focus indicator with a success and a failure colour. */
class FocusRingView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : View(context, attrs, defStyleAttr) {

    private val density = resources.displayMetrics.density

    private val ringPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1.6f * density
    }

    private val neutral = ContextCompat.getColor(context, R.color.shutter_white)
    private val success = ContextCompat.getColor(context, R.color.level_green)
    private val failure = ContextCompat.getColor(context, R.color.warning_amber)

    private var focusX = 0f
    private var focusY = 0f
    private var progress = 0f
    private var visibleAlpha = 0f
    private var animator: ValueAnimator? = null

    init {
        isClickable = false
        isFocusable = false
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
        ringPaint.color = neutral
    }

    /** Starts the shrinking ring at the touch point. */
    fun show(x: Float, y: Float) {
        focusX = x
        focusY = y
        ringPaint.color = neutral
        animator?.cancel()
        val duration = Motion.scaled(context, Motion.SLOW)
        if (duration == 0L) {
            progress = 1f
            visibleAlpha = 1f
            invalidate()
            return
        }
        animator = ValueAnimator.ofFloat(0f, 1f).apply {
            this.duration = duration
            interpolator = Motion.EMPHASIZED
            addUpdateListener {
                progress = it.animatedValue as Float
                visibleAlpha = 1f
                invalidate()
            }
            start()
        }
    }

    /** Colours the ring and fades it out. */
    fun finish(successful: Boolean) {
        ringPaint.color = if (successful) success else failure
        animator?.cancel()
        val duration = Motion.scaled(context, Motion.SLOW * 2)
        if (duration == 0L) {
            visibleAlpha = 0f
            invalidate()
            return
        }
        animator = ValueAnimator.ofFloat(1f, 0f).apply {
            this.duration = duration
            startDelay = Motion.scaled(context, Motion.MEDIUM)
            addUpdateListener {
                visibleAlpha = it.animatedValue as Float
                invalidate()
            }
            start()
        }
    }

    fun hide() {
        animator?.cancel()
        visibleAlpha = 0f
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        if (visibleAlpha <= 0.01f) return
        val outer = 38f * density
        val radius = outer * (1f - 0.35f * progress)
        ringPaint.alpha = (255 * visibleAlpha).toInt().coerceIn(0, 255)
        canvas.drawCircle(focusX, focusY, radius, ringPaint)
        canvas.drawCircle(focusX, focusY, 3f * density, ringPaint)
    }

    override fun onDetachedFromWindow() {
        animator?.cancel()
        animator = null
        super.onDetachedFromWindow()
    }
}
