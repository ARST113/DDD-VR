package top.rootu.dddvr.vr.activity

import android.content.Intent
import android.net.Uri
import android.util.Log
import top.rootu.dddvr.model.StereoInputType
import top.rootu.dddvr.utils.StereoTypeDetector
import top.rootu.dddvr.vr.model.ProjectionMode
import top.rootu.dddvr.vr.model.StereoLayout
import top.rootu.dddvr.vr.model.StereoPacking
import top.rootu.dddvr.vr.model.VrPlaybackConfig
import top.rootu.dddvr.vr.projection.ProjectionType
import top.rootu.dddvr.vr.stereo.StereoInputMode

object VrIntentParser {
    private fun getLongOrIntExtraCompat(intent: Intent, key: String, defaultValue: Long): Long {
        val b = intent.extras
        val raw = b?.get(key)
        return when (raw) {
            is Long -> raw
            is Int -> raw.toLong()
            is String -> raw.toLongOrNull() ?: defaultValue
            else -> if (intent.hasExtra(key)) intent.getLongExtra(key, defaultValue) else defaultValue
        }
    }
    private const val EXTRA_STEREO_MODE = "stereo_mode"
    private const val EXTRA_START_POSITION_MS = "start_position_ms"
    private const val EXTRA_PROJECTION = "projection"
    private const val EXTRA_STEREO_LAYOUT = "stereo_layout"
    private const val EXTRA_STEREO_PACKING = "stereo_packing"
    private const val EXTRA_VR_PROJECTION = "vr_projection"
    private const val EXTRA_SWAP_EYES = "swap_eyes"
    private const val EXTRA_START_POSITION = "position"

    fun parse(intent: Intent): VrPlaybackRequest? {
        val uri = intent.data ?: intent.getParcelableExtra<Uri>(Intent.EXTRA_STREAM) ?: return null
        val mode = when (intent.getStringExtra(EXTRA_STEREO_MODE)?.lowercase()) {
            "sbs" -> StereoInputMode.SBS
            "sbs_reversed" -> StereoInputMode.SBS_REVERSED
            "ou" -> StereoInputMode.OU
            "ou_reversed" -> StereoInputMode.OU_REVERSED
            null -> inferStereoInputMode(uri)
            else -> StereoInputMode.MONO
        }
        val projection = when (intent.getStringExtra(EXTRA_PROJECTION)?.lowercase()) {
            "curved" -> ProjectionType.CURVED
            "equirect_180" -> ProjectionType.EQUIRECT_180
            "equirect_360" -> ProjectionType.EQUIRECT_360
            else -> ProjectionType.FLAT
        }
        val startPositionMs = getLongOrIntExtraCompat(intent, EXTRA_START_POSITION_MS, getLongOrIntExtraCompat(intent, EXTRA_START_POSITION, 0L)).coerceAtLeast(0L)
        val hasStereoLayoutExtra = intent.hasExtra(EXTRA_STEREO_LAYOUT)
        val layout = when (intent.getStringExtra(EXTRA_STEREO_LAYOUT)?.lowercase()) {
            "sbs" -> StereoLayout.SBS
            "ou" -> StereoLayout.OU
            else -> StereoLayout.MONO
        }
        val packing = when (intent.getStringExtra(EXTRA_STEREO_PACKING)?.lowercase()) {
            "half" -> StereoPacking.HALF
            else -> StereoPacking.FULL
        }
        val hasVrProjectionExtra = intent.hasExtra(EXTRA_VR_PROJECTION)
        val projectionMode = when (intent.getStringExtra(EXTRA_VR_PROJECTION)?.lowercase()) {
            "vr180" -> ProjectionMode.VR180
            "vr360" -> ProjectionMode.VR360
            "curved" -> ProjectionMode.VR_CURVED_SCREEN
            "flat_vr_screen" -> ProjectionMode.VR_FLAT_SCREEN
            else -> ProjectionMode.VR_FLAT_SCREEN
        }
        val swapEyes = intent.getBooleanExtra(EXTRA_SWAP_EYES, false)
        runCatching {
            Log.i("DDDVR/Intent", "uri=$uri stereo=$mode projection=$projection startMs=$startPositionMs type=${intent.type}")
        }
        return VrPlaybackRequest(
            uri = uri,
            title = intent.getStringExtra(Intent.EXTRA_TITLE),
            stereoInputMode = mode,
            projectionType = projection,
            startPositionMs = startPositionMs,
            vrConfig = VrPlaybackConfig(
                projectionMode = projectionMode,
                stereoLayout = layout,
                stereoPacking = packing,
                swapEyes = swapEyes
            ),
            hasStereoLayoutExtra = hasStereoLayoutExtra,
            hasVrProjectionExtra = hasVrProjectionExtra
        )
    }

    private fun inferStereoInputMode(uri: Uri): StereoInputMode {
        return when (StereoTypeDetector.detect(format = null, uri = uri)) {
            StereoInputType.SIDE_BY_SIDE -> StereoInputMode.SBS
            StereoInputType.TOP_BOTTOM -> StereoInputMode.OU
            else -> StereoInputMode.MONO
        }
    }
}
