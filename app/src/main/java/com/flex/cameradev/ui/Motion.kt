package com.flex.cameradev.ui

import android.content.Context
import android.provider.Settings
import android.view.View
import android.view.animation.PathInterpolator

/**
 * Animation timings and the system "remove animations" setting.
 *
 * Every duration goes through [scaled], so a user who turned animations off in
 * the accessibility settings gets instant transitions instead of a shorter one.
 */
object Motion {

    const val FAST = 150L
    const val MEDIUM = 220L
    const val SLOW = 300L

    /** Smooth, slightly decelerating curve used across the interface. */
    val STANDARD = PathInterpolator(0.2f, 0f, 0f, 1f)

    val EMPHASIZED = PathInterpolator(0.3f, 0f, 0.1f, 1f)

    private var cachedScale = -1f

    fun animatorScale(context: Context): Float {
        if (cachedScale >= 0f) return cachedScale
        val scale = try {
            Settings.Global.getFloat(
                context.contentResolver,
                Settings.Global.ANIMATOR_DURATION_SCALE,
                1f,
            )
        } catch (e: SecurityException) {
            1f
        }
        cachedScale = if (scale.isNaN() || scale < 0f) 1f else scale
        return cachedScale
    }

    /** Duration adjusted to the system setting; 0 means "apply immediately". */
    fun scaled(context: Context, duration: Long): Long =
        (duration * animatorScale(context)).toLong().coerceAtLeast(0L)

    /** Fades a view in or out, skipping the work when it is already there. */
    fun fade(view: View, visible: Boolean, duration: Long = MEDIUM) {
        val target = if (visible) 1f else 0f
        if (view.alpha == target && view.isVisible() == visible) return
        val scaledDuration = scaled(view.context, duration)
        if (scaledDuration == 0L) {
            view.alpha = target
            view.visibility = if (visible) View.VISIBLE else View.GONE
            return
        }
        if (visible) view.visibility = View.VISIBLE
        view.animate()
            .alpha(target)
            .setDuration(scaledDuration)
            .setInterpolator(STANDARD)
            .withEndAction {
                view.visibility = if (visible) View.VISIBLE else View.GONE
            }
            .start()
    }

    private fun View.isVisible(): Boolean = visibility == View.VISIBLE
}
