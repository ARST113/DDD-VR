package top.rootu.dddvr.utils

import android.net.Uri
import androidx.media3.common.C
import androidx.media3.common.Format
import top.rootu.dddvr.model.StereoInputType
import top.rootu.dddvr.vr.stereo.StereoInputMode
import java.util.Locale
import java.util.regex.Pattern

object StereoTypeDetector {
    private const val SEP = "[\\.\\-_ \\[\\]\\(\\)\\{\\}]+"
    private const val B_START = "(^|[\\.\\-_ \\[\\]\\(\\)\\{\\}])"
    private const val B_END = "([\\.\\-_ \\[\\]\\(\\)\\{\\}]|$)"
    private const val JOIN = "[\\.\\-_ \\[\\]\\(\\)\\{\\}]*"

    private val VR_CAM_V1_PATTERN = regex(
        "${B_START}(vr${JOIN}cam${JOIN}(v)?1|vrcam(v)?1|vr${JOIN}camera${JOIN}(v)?1)${B_END}"
    )
    private val VR_CAM_V2_PATTERN = regex(
        "${B_START}(vr${JOIN}cam${JOIN}(v)?2|vrcam(v)?2|vr${JOIN}camera${JOIN}(v)?2)${B_END}"
    )

    private val SBS_REVERSED_PATTERN = regex(
        "${B_START}(sbs${JOIN}(reverse|reversed|rl|right${JOIN}left)|rlq?|right${JOIN}left|right${JOIN}first|lr${JOIN}reverse|side${JOIN}by${JOIN}side${JOIN}reverse)${B_END}"
    )
    private val SBS_PATTERN = regex(
        "${B_START}((half|full|h|f)?${JOIN}sbs|lrq?|left${JOIN}right|left${JOIN}first|side${JOIN}by${JOIN}side|3d${JOIN}lr)(3d)?${B_END}"
    )
    private val COMPACT_SBS_PATTERN = regex(
        "((half|full|h|f)?sbs3d|3d(half|full|h|f)?sbs)"
    )

    private val OU_REVERSED_PATTERN = regex(
        "${B_START}((ou|tb)${JOIN}(reverse|reversed|ba|bottom${JOIN}top)|ba|baq|bottom${JOIN}top|below${JOIN}above|bottom${JOIN}first)${B_END}"
    )
    private val OU_PATTERN = regex(
        "${B_START}((half|full|h|f)?${JOIN}(ou|tb)|oubs|abq?|above${JOIN}below|top${JOIN}bottom|over${JOIN}under|3d${JOIN}(tb|ou))(3d)?${B_END}"
    )
    private val COMPACT_OU_PATTERN = regex(
        "((half|full|h|f)?(ou|tb)3d|3d(half|full|h|f)?(ou|tb))"
    )

    private val INTERLACED_PATTERN = regex("${B_START}interlace(d)?${B_END}")
    private val TILED_1080P_PATTERN = regex("${B_START}3d(z)?${SEP}?tiled${SEP}?(format)?${B_END}")

    fun detect(format: Format?, uri: Uri?): StereoInputType {
        if (format != null && format.stereoMode != Format.NO_VALUE) {
            when (format.stereoMode) {
                C.STEREO_MODE_LEFT_RIGHT -> return StereoInputType.SIDE_BY_SIDE
                C.STEREO_MODE_TOP_BOTTOM -> return StereoInputType.TOP_BOTTOM
                C.STEREO_MODE_INTERLEAVED_LEFT_PRIMARY,
                C.STEREO_MODE_INTERLEAVED_RIGHT_PRIMARY -> return StereoInputType.INTERLACED
                else -> Unit
            }
        }

        val filename = normalizedFilename(uri) ?: return StereoInputType.NONE
        return when {
            SBS_REVERSED_PATTERN.matcher(filename).find() -> StereoInputType.SIDE_BY_SIDE
            SBS_PATTERN.matcher(filename).find() -> StereoInputType.SIDE_BY_SIDE
            COMPACT_SBS_PATTERN.matcher(filename).find() -> StereoInputType.SIDE_BY_SIDE
            OU_REVERSED_PATTERN.matcher(filename).find() -> StereoInputType.TOP_BOTTOM
            OU_PATTERN.matcher(filename).find() -> StereoInputType.TOP_BOTTOM
            COMPACT_OU_PATTERN.matcher(filename).find() -> StereoInputType.TOP_BOTTOM
            INTERLACED_PATTERN.matcher(filename).find() -> StereoInputType.INTERLACED
            TILED_1080P_PATTERN.matcher(filename).find() -> StereoInputType.TILED_1080P
            else -> StereoInputType.NONE
        }
    }

    fun detectStereoInputMode(format: Format?, uri: Uri?): StereoInputMode? {
        if (format != null && format.stereoMode != Format.NO_VALUE) {
            when (format.stereoMode) {
                C.STEREO_MODE_LEFT_RIGHT -> return StereoInputMode.SBS
                C.STEREO_MODE_TOP_BOTTOM -> return StereoInputMode.OU
                else -> Unit
            }
        }

        val filename = normalizedFilename(uri) ?: return null
        return when {
            VR_CAM_V1_PATTERN.matcher(filename).find() -> StereoInputMode.VR_CAM_V1
            VR_CAM_V2_PATTERN.matcher(filename).find() -> StereoInputMode.VR_CAM_V2
            SBS_REVERSED_PATTERN.matcher(filename).find() -> StereoInputMode.SBS_REVERSED
            SBS_PATTERN.matcher(filename).find() -> StereoInputMode.SBS
            COMPACT_SBS_PATTERN.matcher(filename).find() -> StereoInputMode.SBS
            OU_REVERSED_PATTERN.matcher(filename).find() -> StereoInputMode.OU_REVERSED
            OU_PATTERN.matcher(filename).find() -> StereoInputMode.OU
            COMPACT_OU_PATTERN.matcher(filename).find() -> StereoInputMode.OU
            else -> null
        }
    }

    private fun normalizedFilename(uri: Uri?): String? {
        val raw = uri?.lastPathSegment ?: uri?.path ?: return null
        return raw.lowercase(Locale.ROOT)
    }

    private fun regex(pattern: String): Pattern {
        return Pattern.compile(pattern, Pattern.CASE_INSENSITIVE)
    }
}
