package com.flex.cameradev.ui.views

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import androidx.core.content.ContextCompat
import com.flex.cameradev.R
import com.flex.cameradev.ui.Motion

/**
 * The capture control: a white disc for stills, a red disc for video and a red
 * rounded square while recording, with a double ring around it.
 */
class ShutterButton @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : View(context, attrs, defStyleAttr) {

    enum class Look { PHOTO, VIDEO_IDLE, VIDEO_RECORDING }

    private val ringPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = ContextCompat.getColor(context, R.color.shutter_white)
    }
    private val innerPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val innerBounds = RectF()

    private val white = ContextCompat.getColor(context, R.color.shutter_white)
    private val red = ContextCompat.getColor(context, R.color.record_red)

    /** 0 = disc, 1 = rounded square. */
    private var squareness = 0f

    /** 0 = white, 1 = red. */
    private var redness = 0f

    private var innerScale = 1f
    private var animator: ValueAnimator? = null

    var look: Look = Look.PHOTO
        private set

    init {
        ringPaint.strokeWidth = resources.displayMetrics.density * 3f
        isClickable = true
        isFocusable = true
        contentDescription = context.getString(R.string.shutter_photo_description)
    }

    fun setLook(next: Look, animate: Boolean = true) {
        if (look == next) return
        look = next
        contentDescription = context.getString(
            if (next == Look.PHOTO) R.string.shutter_photo_description
            else R.string.shutter_video_description,
        )
        val targetSquareness = if (next == Look.VIDEO_RECORDING) 1f else 0f
        val targetRedness = if (next == Look.PHOTO) 0f else 1f
        val targetScale = if (next == Look.VIDEO_RECORDING) 0.62f else 1f
        animateTo(targetSquareness, targetRedness, targetScale, animate)
    }

    private fun animateTo(
        targetSquareness: Float,
        targetRedness: Float,
        targetScale: Float,
        animate: Boolean,
    ) {
        animator?.cancel()
        val duration = if (animate) Motion.scaled(context, Motion.MEDIUM) else 0L
        if (duration == 0L) {
            squareness = targetSquareness
            redness = targetRedness
            innerScale = targetScale
            invalidate()
            return
        }
        val fromSquareness = squareness
        val fromRedness = redness
        val fromScale = innerScale
        animator = ValueAnimator.ofFloat(0f, 1f).apply {
            this.duration = duration
            interpolator = Motion.EMPHASIZED
            addUpdateListener { animation ->
                val t = animation.animatedValue as Float
                squareness = fromSquareness + (targetSquareness - fromSquareness) * t
                redness = fromRedness + (targetRedness - fromRedness) * t
                innerScale = fromScale + (targetScale - fromScale) * t
                invalidate()
            }
            start()
        }
    }

    override fun setEnabled(enabled: Boolean) {
        super.setEnabled(enabled)
        alpha = if (enabled) 1f else 0.45f
    }

    override fun onDraw(canvas: Canvas) {
        val centerX = width / 2f
        val centerY = height / 2f
        val outerRadius = minOf(width, height) / 2f - ringPaint.strokeWidth
        canvas.drawCircle(centerX, centerY, outerRadius, ringPaint)

        val innerRadius = outerRadius * 0.78f * innerScale
        innerPaint.color = blend(white, red, redness)
        innerBounds.set(
            centerX - innerRadius,
            centerY - innerRadius,
            centerX + innerRadius,
            centerY + innerRadius,
        )
        val corner = innerRadius * (1f - squareness) + innerRadius * 0.25f * squareness
        canvas.drawRoundRect(innerBounds, corner, corner, innerPaint)
    }

    private fun blend(from: Int, to: Int, fraction: Float): Int {
        val t = fraction.coerceIn(0f, 1f)
        val inverse = 1f - t
        val a = ((from ushr 24 and 0xFF) * inverse + (to ushr 24 and 0xFF) * t).toInt()
        val r = ((from ushr 16 and 0xFF) * inverse + (to ushr 16 and 0xFF) * t).toInt()
        val g = ((from ushr 8 and 0xFF) * inverse + (to ushr 8 and 0xFF) * t).toInt()
        val b = ((from and 0xFF) * inverse + (to and 0xFF) * t).toInt()
        return (a shl 24) or (r shl 16) or (g shl 8) or b
    }

    override fun onDetachedFromWindow() {
        animator?.cancel()
        animator = null
        super.onDetachedFromWindow()
    }
}
