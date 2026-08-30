package com.flex.cameradev.horizon

import android.util.Log
import androidx.camera.core.CameraEffect
import androidx.camera.core.SurfaceProcessor
import androidx.core.util.Consumer
import com.flex.cameradev.horizon.gl.HorizonSurfaceProcessor
import java.util.concurrent.Executor
import java.util.concurrent.Executors

/** CameraEffect that routes preview and recording through the horizon renderer. */
private class HorizonCameraEffect(
    executor: Executor,
    processor: SurfaceProcessor,
    errorListener: Consumer<Throwable>,
) : CameraEffect(
    PREVIEW or VIDEO_CAPTURE,
    executor,
    processor,
    errorListener,
)

/**
 * Owns the lifetime of the GPU pipeline.
 *
 * The effect is created only while a horizon mode is active, so an ordinary
 * recording never pays for the extra pass.
 */
class VideoRenderPipeline(
    private val correction: HorizonSurfaceProcessor.CorrectionSource,
    private val onError: (Throwable) -> Unit,
) {

    private var processor: HorizonSurfaceProcessor? = null
    private var effect: CameraEffect? = null
    private var callbackExecutor: java.util.concurrent.ExecutorService? = null

    val isActive: Boolean get() = effect != null

    /** Creates the effect, reusing the running one when it is still valid. */
    fun acquireEffect(): CameraEffect {
        effect?.let { return it }
        val executor = Executors.newSingleThreadExecutor()
        val surfaceProcessor = HorizonSurfaceProcessor(correction)
        val created = HorizonCameraEffect(
            executor,
            surfaceProcessor,
        ) { throwable ->
            Log.e(TAG, "Fallo en el efecto de horizonte", throwable)
            onError(throwable)
        }
        processor = surfaceProcessor
        callbackExecutor = executor
        effect = created
        return created
    }

    /** Tears the pipeline down. The caller must unbind the use cases first. */
    fun release() {
        processor?.release()
        processor = null
        effect = null
        callbackExecutor?.shutdown()
        callbackExecutor = null
    }

    private companion object {
        const val TAG = "VideoRenderPipeline"
    }
}
