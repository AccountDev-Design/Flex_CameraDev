package com.flex.cameradev.ui.views

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import androidx.core.content.ContextCompat
import com.flex.cameradev.R
import com.flex.cameradev.core.ZoomMath

/**
 * Logarithmic zoom slider from the camera minimum to 1000x.
 *
 * The travel is logarithmic, so the useful optical range is not squeezed into
 * the first pixels, and the quick stops are drawn as ticks.
 */
class ZoomSlider @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : View(context, attrs, defStyleAttr) {

    fun interface OnRatioChanged {
        fun onChanged(ratio: Float, fromUser: Boolean)
    }

    private val trackPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = ContextCompat.getColor(context, R.color.glass_tint_soft)
    }
    private val activePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = ContextCompat.getColor(context, R.color.accent_cyan)
    }
    private val tickPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = ContextCompat.getColor(context, R.color.text_disabled)
    }
    private val thumbPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = ContextCompat.getColor(context, R.color.shutter_white)
    }
    private val cameraLimitPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = ContextCompat.getColor(context, R.color.warning_amber)
    }

    private val trackBounds = RectF()
    private val density = resources.displayMetrics.density

    private var minRatio = ZoomMath.MIN_RATIO
    private var maxRatio = ZoomMath.MAX_DIGITAL_RATIO
    private var cameraMaxRatio = ZoomMath.MIN_RATIO
    private var ratio = ZoomMath.MIN_RATIO

    var listener: OnRatioChanged? = null

    init {
        isClickable = true
        isFocusable = true
        contentDescription = context.getString(R.string.zoom_slider_description)
    }

    /** Updates the range without firing a user change. */
    fun setRange(minimum: Float, maximum: Float, cameraMaximum: Float) {
        minRatio = ZoomMath.sanitize(minimum, ZoomMath.MIN_RATIO).coerceAtLeast(0.1f)
        maxRatio = ZoomMath.sanitize(maximum, ZoomMath.MAX_DIGITAL_RATIO)
            .coerceAtLeast(minRatio * 1.01f)
        cameraMaxRatio = ZoomMath.clamp(cameraMaximum, minRatio, maxRatio)
        ratio = ZoomMath.clamp(ratio, minRatio, maxRatio)
        invalidate()
    }

    /** Moves the thumb without notifying, used when the state changes elsewhere. */
    fun setRatioSilently(value: Float) {
        val clamped = ZoomMath.clamp(value, minRatio, maxRatio)
        if (clamped == ratio) return
        ratio = clamped
        invalidate()
    }

    override fun onSizeChanged(width: Int, height: Int, oldWidth: Int, oldHeight: Int) {
        super.onSizeChanged(width, height, oldWidth, oldHeight)
        val trackHeight = 4f * density
        val centerY = height / 2f
        trackBounds.set(
            paddingLeft.toFloat(),
            centerY - trackHeight / 2f,
            (width - paddingRight).toFloat(),
            centerY + trackHeight / 2f,
        )
    }

    override fun onDraw(canvas: Canvas) {
        if (trackBounds.isEmpty) return
        val radius = trackBounds.height() / 2f
        canvas.drawRoundRect(trackBounds, radius, radius, trackPaint)

        val position = ZoomMath.ratioToPosition(ratio, minRatio, maxRatio)
        val thumbX = trackBounds.left + trackBounds.width() * position
        canvas.drawRoundRect(
            RectF(trackBounds.left, trackBounds.top, thumbX, trackBounds.bottom),
            radius,
            radius,
            activePaint,
        )

        for (stop in ZoomMath.QUICK_STOPS) {
            if (stop < minRatio || stop > maxRatio) continue
            val stopX = trackBounds.left +
                trackBounds.width() * ZoomMath.ratioToPosition(stop, minRatio, maxRatio)
            canvas.drawCircle(stopX, trackBounds.centerY(), 2.5f * density, tickPaint)
        }

        if (cameraMaxRatio > minRatio && cameraMaxRatio < maxRatio) {
            val limitX = trackBounds.left +
                trackBounds.width() * ZoomMath.ratioToPosition(cameraMaxRatio, minRatio, maxRatio)
            canvas.drawRect(
                limitX - density,
                trackBounds.centerY() - 9f * density,
                limitX + density,
                trackBounds.centerY() + 9f * density,
                cameraLimitPaint,
            )
        }

        canvas.drawCircle(thumbX, trackBounds.centerY(), 9f * density, thumbPaint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!isEnabled) return false
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE -> {
                parent?.requestDisallowInterceptTouchEvent(true)
                updateFromTouch(event.x)
                return true
            }

            MotionEvent.ACTION_UP -> {
                parent?.requestDisallowInterceptTouchEvent(false)
                updateFromTouch(event.x)
                performClick()
                return true
            }

            MotionEvent.ACTION_CANCEL -> {
                parent?.requestDisallowInterceptTouchEvent(false)
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    private fun updateFromTouch(x: Float) {
        if (trackBounds.width() <= 0f) return
        val position = ((x - trackBounds.left) / trackBounds.width()).coerceIn(0f, 1f)
        val next = ZoomMath.positionToRatio(position, minRatio, maxRatio)
        if (next == ratio) return
        ratio = next
        invalidate()
        listener?.onChanged(next, true)
    }
}
