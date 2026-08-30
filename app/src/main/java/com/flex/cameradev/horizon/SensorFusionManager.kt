package com.flex.cameradev.horizon

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import com.flex.cameradev.core.AngleSmoother
import com.flex.cameradev.core.ComplementaryRollEstimator
import com.flex.cameradev.core.HorizonMath

/** Which sensor chain the device actually offers. */
enum class RollSource {
    ROTATION_VECTOR,
    GAME_ROTATION_VECTOR,
    ACCELEROMETER_GYROSCOPE,
    ACCELEROMETER_ONLY,
    NONE,
}

/**
 * Turns the IMU into a smoothed roll angle.
 *
 * The fused rotation vector is preferred because the platform already removes
 * the gyroscope drift; the accelerometer and gyroscope chain is only used when
 * no fused sensor exists, and then the drift is corrected explicitly.
 */
class SensorFusionManager(context: Context) : SensorEventListener {

    fun interface Listener {
        /** Called on the sensor thread with the smoothed, display corrected roll. */
        fun onRoll(rollDegrees: Float)
    }

    private val sensorManager =
        context.applicationContext.getSystemService(Context.SENSOR_SERVICE) as? SensorManager

    private val rotationVector = sensorManager?.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
    private val gameRotationVector = sensorManager?.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
    private val accelerometer = sensorManager?.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
    private val gyroscope = sensorManager?.getDefaultSensor(Sensor.TYPE_GYROSCOPE)

    val source: RollSource = when {
        rotationVector != null -> RollSource.ROTATION_VECTOR
        gameRotationVector != null -> RollSource.GAME_ROTATION_VECTOR
        accelerometer != null && gyroscope != null -> RollSource.ACCELEROMETER_GYROSCOPE
        accelerometer != null -> RollSource.ACCELEROMETER_ONLY
        else -> RollSource.NONE
    }

    val isAvailable: Boolean get() = source != RollSource.NONE

    private val rotationMatrix = FloatArray(9)
    private val smoother = AngleSmoother(timeConstantSeconds = 0.10f)
    private val complementary = ComplementaryRollEstimator(driftCorrectionSeconds = 1.2f)

    private var thread: HandlerThread? = null
    private var handler: Handler? = null
    private var listener: Listener? = null

    private var lastEventNanos = 0L
    private var pendingAccelerometerRoll: Float? = null

    @Volatile
    var displayRotationDegrees: Int = 0

    /** Latest smoothed value, also readable from other threads. */
    @Volatile
    var currentRoll: Float = 0f
        private set

    fun start(listener: Listener) {
        val manager = sensorManager ?: return
        if (!isAvailable || thread != null) return
        this.listener = listener
        lastEventNanos = 0L
        pendingAccelerometerRoll = null
        smoother.reset(0f)
        complementary.reset(0f)
        val sensorThread = HandlerThread("A55-Imu").apply { start() }
        val sensorHandler = Handler(sensorThread.looper)
        thread = sensorThread
        handler = sensorHandler
        when (source) {
            RollSource.ROTATION_VECTOR ->
                manager.registerListener(this, rotationVector, SAMPLING_PERIOD_US, sensorHandler)

            RollSource.GAME_ROTATION_VECTOR ->
                manager.registerListener(this, gameRotationVector, SAMPLING_PERIOD_US, sensorHandler)

            RollSource.ACCELEROMETER_GYROSCOPE -> {
                manager.registerListener(this, accelerometer, SAMPLING_PERIOD_US, sensorHandler)
                manager.registerListener(this, gyroscope, SAMPLING_PERIOD_US, sensorHandler)
            }

            RollSource.ACCELEROMETER_ONLY ->
                manager.registerListener(this, accelerometer, SAMPLING_PERIOD_US, sensorHandler)

            RollSource.NONE -> Unit
        }
    }

    fun stop() {
        sensorManager?.unregisterListener(this)
        listener = null
        thread?.quitSafely()
        thread = null
        handler = null
    }

    override fun onSensorChanged(event: SensorEvent) {
        val deltaSeconds = deltaSeconds(event.timestamp)
        val roll = when (event.sensor.type) {
            Sensor.TYPE_ROTATION_VECTOR, Sensor.TYPE_GAME_ROTATION_VECTOR ->
                rollFromVector(event)

            Sensor.TYPE_ACCELEROMETER -> {
                val measured = HorizonMath.rollFromGravity(event.values[0], event.values[1])
                pendingAccelerometerRoll = measured
                if (gyroscope == null) {
                    complementary.update(0f, measured, deltaSeconds)
                } else {
                    null
                }
            }

            Sensor.TYPE_GYROSCOPE -> {
                // values[2] is the rotation rate around the device Z axis, in rad/s.
                val rateDegPerSecond = (event.values[2] * 180f / Math.PI).toFloat()
                val fused = complementary.update(
                    rateDegPerSecond,
                    pendingAccelerometerRoll,
                    deltaSeconds,
                )
                pendingAccelerometerRoll = null
                fused
            }

            else -> null
        } ?: return

        val compensated = HorizonMath.compensateDisplay(roll, displayRotationDegrees)
        val smoothed = smoother.update(compensated, deltaSeconds)
        currentRoll = smoothed
        listener?.onRoll(smoothed)
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {
        if (accuracy == SensorManager.SENSOR_STATUS_UNRELIABLE) {
            Log.w(TAG, "Precisión del sensor ${sensor?.name} no fiable")
        }
    }

    private fun rollFromVector(event: SensorEvent): Float? = try {
        SensorManager.getRotationMatrixFromVector(rotationMatrix, event.values)
        HorizonMath.rollFromRotationMatrix(rotationMatrix)
    } catch (e: IllegalArgumentException) {
        Log.w(TAG, "Vector de rotación inválido", e)
        null
    }

    private fun deltaSeconds(timestampNanos: Long): Float {
        if (lastEventNanos == 0L) {
            lastEventNanos = timestampNanos
            return 0f
        }
        val delta = (timestampNanos - lastEventNanos).coerceAtLeast(0L)
        lastEventNanos = timestampNanos
        return (delta / 1_000_000_000.0).toFloat().coerceIn(0f, 0.25f)
    }

    private companion object {
        const val TAG = "SensorFusionManager"

        /** 100 Hz: fast enough for video, cheap enough to stay off the main thread. */
        const val SAMPLING_PERIOD_US = 10_000
    }
}
