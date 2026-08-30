package com.flex.cameradev.ui

import android.content.Context
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager

/**
 * Short confirmation ticks only.
 *
 * The zoom fires one tick when a stop is crossed, never a continuous vibration
 * while the slider moves.
 */
class Haptics(context: Context) {

    private val vibrator: Vibrator? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        (context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager)
            ?.defaultVibrator
    } else {
        @Suppress("DEPRECATION")
        context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
    }

    private val available: Boolean = vibrator?.hasVibrator() == true

    fun tick() = play(TICK_MILLIS, VibrationEffect.DEFAULT_AMPLITUDE)

    fun confirm() = play(CONFIRM_MILLIS, VibrationEffect.DEFAULT_AMPLITUDE)

    private fun play(durationMillis: Long, amplitude: Int) {
        if (!available) return
        val effect = VibrationEffect.createOneShot(durationMillis, amplitude)
        vibrator?.vibrate(effect)
    }

    private companion object {
        const val TICK_MILLIS = 12L
        const val CONFIRM_MILLIS = 24L
    }
}
