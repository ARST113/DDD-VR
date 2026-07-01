package top.rootu.dddvr.vr.activity

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mockito.ArgumentMatchers.anyBoolean
import org.mockito.ArgumentMatchers.anyLong
import org.mockito.ArgumentMatchers.anyString
import org.mockito.Mockito.mock
import org.mockito.Mockito.`when`
import top.rootu.dddvr.vr.model.ProjectionMode
import top.rootu.dddvr.vr.model.StereoLayout
import top.rootu.dddvr.vr.stereo.StereoInputMode

class VrIntentParserTest {

    private fun mockedIntent(
        stringExtras: Map<String, String> = emptyMap(),
        boolExtras: Map<String, Boolean> = emptyMap(),
        rawExtras: Map<String, Any> = emptyMap(),
        uriLastPathSegment: String? = null
    ): Intent {
        val intent = mock(Intent::class.java)
        val uri = mock(Uri::class.java)
        val extras = mock(Bundle::class.java)
        val allExtras = mutableMapOf<String, Any>().apply {
            putAll(rawExtras)
            putAll(stringExtras)
            putAll(boolExtras)
        }

        `when`(intent.data).thenReturn(uri)
        `when`(intent.extras).thenReturn(extras)
        `when`(uri.lastPathSegment).thenReturn(uriLastPathSegment)
        `when`(intent.hasExtra(anyString())).thenAnswer { allExtras.containsKey(it.getArgument(0)) }
        `when`(intent.getStringExtra(anyString())).thenAnswer { allExtras[it.getArgument<String>(0)] as? String }
        `when`(intent.getLongExtra(anyString(), anyLong())).thenAnswer {
            val key = it.getArgument<String>(0)
            val defaultValue = it.getArgument<Long>(1)
            when (val value = allExtras[key]) {
                is Long -> value
                is Int -> value.toLong()
                is String -> value.toLongOrNull() ?: defaultValue
                else -> defaultValue
            }
        }
        `when`(intent.getBooleanExtra(anyString(), anyBoolean())).thenAnswer {
            val key = it.getArgument<String>(0)
            val defaultValue = it.getArgument<Boolean>(1)
            (allExtras[key] as? Boolean) ?: defaultValue
        }

        `when`(extras.get(anyString())).thenAnswer { allExtras[it.getArgument<String>(0)] }
        `when`(extras.containsKey(anyString())).thenAnswer { allExtras.containsKey(it.getArgument(0)) }

        return intent
    }

    @Test
    fun `parses vr extras and position fallback`() {
        val intent = mockedIntent(
            stringExtras = mapOf("vr_projection" to "vr180", "stereo_layout" to "sbs"),
            boolExtras = mapOf("swap_eyes" to true),
            rawExtras = mapOf("position" to 120_000L)
        )

        val parsed = VrIntentParser.parse(intent)

        requireNotNull(parsed)
        assertEquals(ProjectionMode.VR180, parsed.vrConfig.projectionMode)
        assertEquals(StereoLayout.SBS, parsed.vrConfig.stereoLayout)
        assertTrue(parsed.vrConfig.swapEyes)
        assertEquals(120_000L, parsed.startPositionMs)
    }

    @Test
    fun `parses vr360 and ou stereo layout`() {
        val intent = mockedIntent(
            stringExtras = mapOf("vr_projection" to "vr360", "stereo_layout" to "ou")
        )

        val parsed = VrIntentParser.parse(intent)

        requireNotNull(parsed)
        assertEquals(ProjectionMode.VR360, parsed.vrConfig.projectionMode)
        assertEquals(StereoLayout.OU, parsed.vrConfig.stereoLayout)
    }

    @Test
    fun `parses position as Int extra`() {
        val intent = mockedIntent(rawExtras = mapOf("position" to 120000))

        val parsed = VrIntentParser.parse(intent)

        requireNotNull(parsed)
        assertEquals(120_000L, parsed.startPositionMs)
    }

    @Test
    fun `marks stereo_layout extra presence for mono override`() {
        val intent = mockedIntent(
            stringExtras = mapOf("stereo_mode" to "sbs", "stereo_layout" to "mono")
        )

        val parsed = VrIntentParser.parse(intent)

        requireNotNull(parsed)
        assertTrue(parsed.hasStereoLayoutExtra)
        assertEquals(StereoLayout.MONO, parsed.vrConfig.stereoLayout)
    }

    @Test
    fun `uses stereo layout aliases as stereo mode fallback without mono override`() {
        val sbsIntent = mockedIntent(stringExtras = mapOf("stereo_layout" to "rl"))
        val ouIntent = mockedIntent(stringExtras = mapOf("stereo_layout" to "ba"))

        val sbsParsed = VrIntentParser.parse(sbsIntent)
        val ouParsed = VrIntentParser.parse(ouIntent)

        requireNotNull(sbsParsed)
        requireNotNull(ouParsed)
        assertEquals(StereoInputMode.SBS_REVERSED, sbsParsed.stereoInputMode)
        assertEquals(StereoLayout.MONO, sbsParsed.vrConfig.stereoLayout)
        assertEquals(false, sbsParsed.hasStereoLayoutExtra)
        assertEquals(StereoInputMode.OU_REVERSED, ouParsed.stereoInputMode)
        assertEquals(StereoLayout.MONO, ouParsed.vrConfig.stereoLayout)
        assertEquals(false, ouParsed.hasStereoLayoutExtra)
    }
    @Test
    fun `infers over under stereo mode from filename`() {
        val intent = mockedIntent(uriLastPathSegment = "Avatar 2 (2022) 3D-hOU.mkv")

        val parsed = VrIntentParser.parse(intent)

        requireNotNull(parsed)
        assertEquals(StereoInputMode.OU, parsed.stereoInputMode)
    }

    @Test
    fun `infers oubs stereo mode from filename`() {
        val intent = mockedIntent(uriLastPathSegment = "Movie.3D.OUBS.mkv")

        val parsed = VrIntentParser.parse(intent)

        requireNotNull(parsed)
        assertEquals(StereoInputMode.OU, parsed.stereoInputMode)
    }
    @Test
    fun `infers sbs stereo mode when marker starts filename`() {
        val intent = mockedIntent(uriLastPathSegment = "SBS.Avatar.The.Way.of.Water.mkv")

        val parsed = VrIntentParser.parse(intent)

        requireNotNull(parsed)
        assertEquals(StereoInputMode.SBS, parsed.stereoInputMode)
    }

    @Test
    fun `infers compact full sbs 3d marker from stream url`() {
        val intent = mockedIntent(uriLastPathSegment = "AvatarTheWayofWater 2022 (1080p24fpsH264FullSBS3D).mkv")

        val parsed = VrIntentParser.parse(intent)

        requireNotNull(parsed)
        assertEquals(StereoInputMode.SBS, parsed.stereoInputMode)
    }

    @Test
    fun `infers reversed stereo modes from filename aliases`() {
        val sbsIntent = mockedIntent(uriLastPathSegment = "Movie.3D.RL.mkv")
        val ouIntent = mockedIntent(uriLastPathSegment = "Movie.3D.BA.mkv")

        val sbsParsed = VrIntentParser.parse(sbsIntent)
        val ouParsed = VrIntentParser.parse(ouIntent)

        requireNotNull(sbsParsed)
        requireNotNull(ouParsed)
        assertEquals(StereoInputMode.SBS_REVERSED, sbsParsed.stereoInputMode)
        assertEquals(StereoInputMode.OU_REVERSED, ouParsed.stereoInputMode)
    }

    @Test
    fun `parses vr cam stereo modes`() {
        val v1Intent = mockedIntent(stringExtras = mapOf("stereo_mode" to "vr-cam-v1"))
        val v2Intent = mockedIntent(uriLastPathSegment = "Avatar.vrcam2.mkv")

        val v1Parsed = VrIntentParser.parse(v1Intent)
        val v2Parsed = VrIntentParser.parse(v2Intent)

        requireNotNull(v1Parsed)
        requireNotNull(v2Parsed)
        assertEquals(StereoInputMode.VR_CAM_V1, v1Parsed.stereoInputMode)
        assertEquals(StereoInputMode.VR_CAM_V2, v2Parsed.stereoInputMode)
    }
}
