package top.rootu.dddvr.vr.activity

import android.content.Intent
import android.net.Uri
import android.util.Log
import top.rootu.dddvr.vr.projection.ProjectionType
import top.rootu.dddvr.vr.stereo.StereoInputMode

object VrIntentParser {
    private const val EXTRA_STEREO_MODE = "stereo_mode"
    private const val EXTRA_START_POSITION_MS = "start_position_ms"
    private const val EXTRA_PROJECTION = "projection"

    fun parse(intent: Intent): VrPlaybackRequest? {
        val uri = intent.data ?: intent.getParcelableExtra<Uri>(Intent.EXTRA_STREAM) ?: return null
        val mode = when (intent.getStringExtra(EXTRA_STEREO_MODE)?.lowercase()) {
            "sbs" -> StereoInputMode.SBS
            "sbs_reversed" -> StereoInputMode.SBS_REVERSED
            "ou" -> StereoInputMode.OU
            "ou_reversed" -> StereoInputMode.OU_REVERSED
            else -> StereoInputMode.MONO
        }
        val projection = when (intent.getStringExtra(EXTRA_PROJECTION)?.lowercase()) {
            "curved" -> ProjectionType.CURVED
            "equirect_180" -> ProjectionType.EQUIRECT_180
            "equirect_360" -> ProjectionType.EQUIRECT_360
            else -> ProjectionType.FLAT
        }
        val startPositionMs = intent.getLongExtra(EXTRA_START_POSITION_MS, 0L).coerceAtLeast(0L)
        Log.i("DDDVR/Intent", "uri=$uri stereo=$mode projection=$projection startMs=$startPositionMs type=${intent.type}")
        return VrPlaybackRequest(
            uri = uri,
            title = intent.getStringExtra(Intent.EXTRA_TITLE),
            stereoInputMode = mode,
            projectionType = projection,
            startPositionMs = startPositionMs
        )
    }
}
