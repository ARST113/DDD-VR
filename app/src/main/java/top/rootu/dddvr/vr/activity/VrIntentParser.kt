package top.rootu.dddvr.vr.activity

import android.content.Intent
import android.net.Uri
import android.util.Log
import top.rootu.dddvr.utils.StereoTypeDetector
import top.rootu.dddvr.vr.model.ProjectionMode
import top.rootu.dddvr.vr.model.StereoLayout
import top.rootu.dddvr.vr.model.StereoPacking
import top.rootu.dddvr.vr.model.VrPlaybackConfig
import top.rootu.dddvr.vr.projection.ProjectionType
import top.rootu.dddvr.vr.stereo.StereoInputMode
import java.util.Locale

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
        val rawStereoMode = intent.getStringExtra(EXTRA_STEREO_MODE)
        val rawStereoLayout = intent.getStringExtra(EXTRA_STEREO_LAYOUT)
        val mode = parseStereoInputMode(rawStereoMode ?: rawStereoLayout, uri)
        val projection = parseProjectionType(intent.getStringExtra(EXTRA_PROJECTION))
        val startPositionMs = getLongOrIntExtraCompat(
            intent,
            EXTRA_START_POSITION_MS,
            getLongOrIntExtraCompat(intent, EXTRA_START_POSITION, 0L)
        ).coerceAtLeast(0L)
        val layoutToken = normalizeToken(rawStereoLayout)
        val layout = when (layoutToken) {
            "mono", "2d", "flat", "normal" -> StereoLayout.MONO
            "sbs", "hsbs", "fsbs", "side_by_side", "lr", "left_right" -> StereoLayout.SBS
            "ou", "hou", "tb", "htb", "top_bottom", "over_under", "above_below", "ab" -> StereoLayout.OU
            else -> StereoLayout.MONO
        }
        val hasStereoLayoutExtra = layoutToken in setOf(
            "mono", "2d", "flat", "normal",
            "sbs", "hsbs", "fsbs", "side_by_side", "lr", "left_right",
            "ou", "hou", "tb", "htb", "top_bottom", "over_under", "above_below", "ab"
        )
        val packing = when (normalizeToken(intent.getStringExtra(EXTRA_STEREO_PACKING))) {
            "half", "h", "half_width", "half_height" -> StereoPacking.HALF
            else -> StereoPacking.FULL
        }
        val hasVrProjectionExtra = intent.hasExtra(EXTRA_VR_PROJECTION)
        val projectionMode = parseProjectionMode(intent.getStringExtra(EXTRA_VR_PROJECTION))
        val swapEyes = intent.getBooleanExtra(EXTRA_SWAP_EYES, false)
        runCatching {
            Log.i("DDDVR/Intent", "uri=$uri stereo=$mode projection=$projection vrProjection=$projectionMode startMs=$startPositionMs type=${intent.type}")
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

    private fun parseStereoInputMode(raw: String?, uri: Uri): StereoInputMode {
        return when (normalizeToken(raw)) {
            null -> inferStereoInputMode(uri)
            "mono", "2d", "flat", "normal" -> StereoInputMode.MONO
            "sbs", "hsbs", "fsbs", "side_by_side", "lr", "lrq", "left_right", "left_first" -> StereoInputMode.SBS
            "sbs_reversed", "sbs_reverse", "sbs_rl", "rl_sbs", "rl", "rlq", "right_left", "right_first" -> StereoInputMode.SBS_REVERSED
            "ou", "hou", "tb", "htb", "over_under", "top_bottom", "above_below", "ab", "abq" -> StereoInputMode.OU
            "ou_reversed", "ou_reverse", "tb_reversed", "tb_reverse", "ou_ba", "tb_ba", "ba_ou", "ba_tb", "ba", "baq", "bottom_top", "below_above", "bottom_first" -> StereoInputMode.OU_REVERSED
            "vr_cam_v1", "vr_cam_1", "vrcam_v1", "vrcam1", "vr_camera_v1", "vr_camera_1" -> StereoInputMode.VR_CAM_V1
            "vr_cam_v2", "vr_cam_2", "vrcam_v2", "vrcam2", "vr_camera_v2", "vr_camera_2" -> StereoInputMode.VR_CAM_V2
            else -> inferStereoInputMode(uri)
        }
    }

    private fun parseProjectionType(raw: String?): ProjectionType {
        return when (normalizeToken(raw)) {
            "curved", "curve", "curved_screen" -> ProjectionType.CURVED
            "equirect_180", "equirect180", "vr180", "180", "180sbs" -> ProjectionType.EQUIRECT_180
            "equirect_360", "equirect360", "vr360", "360", "360sbs" -> ProjectionType.EQUIRECT_360
            else -> ProjectionType.FLAT
        }
    }

    private fun parseProjectionMode(raw: String?): ProjectionMode {
        return when (normalizeToken(raw)) {
            "vr180", "180", "equirect_180", "equirect180", "180sbs" -> ProjectionMode.VR180
            "vr360", "360", "equirect_360", "equirect360", "360sbs" -> ProjectionMode.VR360
            "curved", "curve", "curved_screen", "vr_curved_screen" -> ProjectionMode.VR_CURVED_SCREEN
            "flat_vr_screen", "flat", "2d", "screen", "vr_flat_screen" -> ProjectionMode.VR_FLAT_SCREEN
            else -> ProjectionMode.VR_FLAT_SCREEN
        }
    }

    private fun inferStereoInputMode(uri: Uri): StereoInputMode {
        return StereoTypeDetector.detectStereoInputMode(format = null, uri = uri) ?: StereoInputMode.MONO
    }

    private fun normalizeToken(value: String?): String? {
        val trimmed = value?.trim()?.takeIf { it.isNotEmpty() } ?: return null
        return trimmed
            .lowercase(Locale.ROOT)
            .replace(Regex("[\\s\\-.]+"), "_")
            .replace(Regex("_+"), "_")
            .trim('_')
    }
}
