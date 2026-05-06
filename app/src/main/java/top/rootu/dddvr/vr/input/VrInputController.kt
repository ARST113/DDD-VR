package top.rootu.dddvr.vr.input

import android.os.SystemClock

class VrInputController(
    var autoHideDelayMs: Long = 5000L,
    private val onShowControls: () -> Unit,
    private val onHideControls: () -> Unit,
    private val isOverlayVisible: () -> Boolean
) {
    var enabled: Boolean = false
        private set
    var lastInteractionTimeMs: Long = 0L
        private set
    var isUserInteractingWithControls: Boolean = false
        private set

    fun enable() {
        enabled = true
        notifyInteraction()
        showControls()
    }

    fun disable() {
        enabled = false
        isUserInteractingWithControls = false
    }

    fun notifyInteraction() {
        lastInteractionTimeMs = SystemClock.uptimeMillis()
    }

    fun notifyControlsInteractionStart() {
        isUserInteractingWithControls = true
        notifyInteraction()
    }

    fun notifyControlsInteractionEnd() {
        isUserInteractingWithControls = false
        notifyInteraction()
    }

    fun shouldAutoHide(nowMs: Long): Boolean {
        return enabled &&
            isOverlayVisible() &&
            !isUserInteractingWithControls &&
            (nowMs - lastInteractionTimeMs) >= autoHideDelayMs
    }

    fun showControls() {
        onShowControls()
        notifyInteraction()
    }

    fun hideControls() = onHideControls()

    fun toggleControls() {
        if (isOverlayVisible()) hideControls() else showControls()
    }
}
