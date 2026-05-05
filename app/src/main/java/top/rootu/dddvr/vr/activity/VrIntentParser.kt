package top.rootu.dddvr.vr.activity

import android.content.Intent
import android.net.Uri
import top.rootu.dddvr.vr.stereo.StereoInputMode

object VrIntentParser {
    private const val EXTRA_STEREO_MODE = "stereo_mode"
    private const val EXTRA_START_POSITION_MS = "start_position_ms"

    fun parse(intent: Intent): VrPlaybackRequest? {
        val uri = intent.data ?: intent.getParcelableExtra<Uri>(Intent.EXTRA_STREAM) ?: return null
        val mode = when (intent.getStringExtra(EXTRA_STEREO_MODE)?.lowercase()) {
            "sbs", "lr", "side_by_side" -> StereoInputMode.SBS
            "ou", "tb", "top_bottom", "over_under" -> StereoInputMode.OU
            else -> StereoInputMode.MONO
        }
        val startPositionMs = intent.getLongExtra(EXTRA_START_POSITION_MS, 0L).coerceAtLeast(0L)
        return VrPlaybackRequest(
            uri = uri,
            title = intent.getStringExtra(Intent.EXTRA_TITLE),
            stereoInputMode = mode,
            startPositionMs = startPositionMs
        )
    }
}
