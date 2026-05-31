package top.rootu.dddvr.vr.ui

import android.view.View
import android.widget.ProgressBar
import android.widget.TextView
import androidx.core.view.isVisible
import top.rootu.dddvr.vr.player.VrPlaybackState

class VrUiLayer(
    val controlsOverlay: VrControlsOverlay,
    private val loading: ProgressBar,
    private val errorText: TextView
) {
    fun show() { controlsOverlay.isVisible = true }
    fun hide() { controlsOverlay.isVisible = false }
    fun isVisible(): Boolean = controlsOverlay.isVisible
    fun update(state: VrPlaybackState, curvedAvailable: Boolean) {
        controlsOverlay.updateState(state, curvedAvailable)
        loading.isVisible = state.durationMs <= 0 &&
            state.positionMs <= 0 &&
            state.bufferedPositionMs <= 0 &&
            !state.isPlaying &&
            !state.hasError
        errorText.isVisible = state.hasError
        errorText.text = state.errorMessage.orEmpty()
    }

    fun setBlockingError(message: String) {
        errorText.text = message
        errorText.visibility = View.VISIBLE
    }
}
