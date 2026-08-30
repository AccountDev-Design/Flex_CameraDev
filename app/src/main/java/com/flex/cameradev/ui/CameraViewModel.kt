package com.flex.cameradev.ui

import androidx.lifecycle.ViewModel
import com.flex.cameradev.camera.DeviceCameraReport
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

/**
 * Holds the only mutable copy of [CameraUiState].
 *
 * Every controller writes through [mutate]; the activity only reads, which is
 * what keeps the labels, the controls and the camera configuration consistent
 * across configuration changes.
 */
class CameraViewModel : ViewModel() {

    private val _state = MutableStateFlow(CameraUiState())
    val state: StateFlow<CameraUiState> = _state.asStateFlow()

    private val _notices = MutableSharedFlow<UiNotice>(extraBufferCapacity = 8)
    val notices: SharedFlow<UiNotice> = _notices.asSharedFlow()

    /** Survives rotation and the diagnostics screen, so the probe runs once. */
    var report: DeviceCameraReport? = null

    val current: CameraUiState get() = _state.value

    fun mutate(block: (CameraUiState) -> CameraUiState) {
        _state.update(block)
    }

    fun notify(notice: UiNotice) {
        _notices.tryEmit(notice)
    }
}
