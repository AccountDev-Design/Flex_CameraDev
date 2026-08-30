package com.flex.cameradev.camera

import androidx.camera.camera2.interop.Camera2CameraInfo
import androidx.camera.camera2.interop.ExperimentalCamera2Interop
import androidx.camera.core.CameraFilter
import androidx.camera.core.CameraInfo
import androidx.camera.core.CameraSelector

/**
 * Builds the CameraSelector for a camera id that was chosen by looking at the
 * real characteristics, instead of assuming the main sensor is id "0".
 */
object CameraSelectorManager {

    /**
     * Selector restricted to [cameraId]. When that id is not exposed to CameraX
     * the filter returns the original list, so binding still succeeds with the
     * default rear camera rather than throwing.
     */
    fun selectorFor(cameraId: String?): CameraSelector {
        val builder = CameraSelector.Builder()
            .requireLensFacing(CameraSelector.LENS_FACING_BACK)
        if (!cameraId.isNullOrBlank()) {
            builder.addCameraFilter(
                CameraFilter { cameraInfos ->
                    val matching = cameraInfos.filter { cameraIdOf(it) == cameraId }
                    matching.ifEmpty { cameraInfos }
                },
            )
        }
        return builder.build()
    }

    @androidx.annotation.OptIn(markerClass = [ExperimentalCamera2Interop::class])
    fun cameraIdOf(cameraInfo: CameraInfo): String? = try {
        Camera2CameraInfo.from(cameraInfo).cameraId
    } catch (e: IllegalArgumentException) {
        null
    }
}
