package com.flex.cameradev.ui.views

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.util.AttributeSet
import android.view.View

/** Rule of thirds guide drawn with two hairline paints and no layout work. */
class GridOverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : View(context, attrs, defStyleAttr) {

    private val linePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0x40FFFFFF
        strokeWidth = context.resources.displayMetrics.density
        style = Paint.Style.STROKE
    }

    init {
        isClickable = false
        isFocusable = false
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    override fun onDraw(canvas: Canvas) {
        val thirdWidth = width / 3f
        val thirdHeight = height / 3f
        for (i in 1..2) {
            canvas.drawLine(thirdWidth * i, 0f, thirdWidth * i, height.toFloat(), linePaint)
            canvas.drawLine(0f, thirdHeight * i, width.toFloat(), thirdHeight * i, linePaint)
        }
    }
}
