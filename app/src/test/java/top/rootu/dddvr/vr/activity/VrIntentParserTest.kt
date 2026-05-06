package top.rootu.dddvr.vr.activity

import android.content.Intent
import android.net.Uri
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import top.rootu.dddvr.vr.model.ProjectionMode
import top.rootu.dddvr.vr.model.StereoLayout

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE)
class VrIntentParserTest {

    @Test
    fun `parses vr extras and position fallback`() {
        val intent = Intent(Intent.ACTION_VIEW).apply {
            data = Uri.parse("https://example.com/video.mp4")
            putExtra("vr_projection", "vr180")
            putExtra("stereo_layout", "sbs")
            putExtra("swap_eyes", true)
            putExtra("position", 120_000L)
        }

        val parsed = VrIntentParser.parse(intent)

        requireNotNull(parsed)
        assertEquals(ProjectionMode.VR180, parsed.vrConfig.projectionMode)
        assertEquals(StereoLayout.SBS, parsed.vrConfig.stereoLayout)
        assertTrue(parsed.vrConfig.swapEyes)
        assertEquals(120_000L, parsed.startPositionMs)
    }

    @Test
    fun `parses vr360 and ou stereo layout`() {
        val intent = Intent(Intent.ACTION_VIEW).apply {
            data = Uri.parse("https://example.com/video.mp4")
            putExtra("vr_projection", "vr360")
            putExtra("stereo_layout", "ou")
        }

        val parsed = VrIntentParser.parse(intent)

        requireNotNull(parsed)
        assertEquals(ProjectionMode.VR360, parsed.vrConfig.projectionMode)
        assertEquals(StereoLayout.OU, parsed.vrConfig.stereoLayout)
    }

    @Test
    fun `parses position as Int extra`() {
        val intent = Intent(Intent.ACTION_VIEW).apply {
            data = Uri.parse("https://example.com/video.mp4")
            putExtra("position", 120000)
        }

        val parsed = VrIntentParser.parse(intent)

        requireNotNull(parsed)
        assertEquals(120_000L, parsed.startPositionMs)
    }

    @Test
    fun `marks stereo_layout extra presence for mono override`() {
        val intent = Intent(Intent.ACTION_VIEW).apply {
            data = Uri.parse("https://example.com/video.mp4")
            putExtra("stereo_mode", "sbs")
            putExtra("stereo_layout", "mono")
        }

        val parsed = VrIntentParser.parse(intent)

        requireNotNull(parsed)
        assertTrue(parsed.hasStereoLayoutExtra)
        assertEquals(StereoLayout.MONO, parsed.vrConfig.stereoLayout)
    }
}
