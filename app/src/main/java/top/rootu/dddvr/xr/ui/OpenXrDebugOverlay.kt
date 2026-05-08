package top.rootu.dddvr.xr.ui

import android.util.Log
import top.rootu.dddvr.xr.model.OpenXrPlaybackConfig

object OpenXrDebugOverlay {
    private const val TAG = "DDDVR/OpenXR"

    fun logStartup(config: OpenXrPlaybackConfig) {
        Log.i(TAG, "start stereo=${config.stereoMode} swapEyes=${config.swapEyes} screen=${config.screenMode}")
    }

    fun logSessionState(state: String) {
        Log.i(TAG, "session_state=$state")
    }

    fun logSurfaceAttached(isAttached: Boolean) {
        Log.i(TAG, "surface_attached=$isAttached")
    }
}
