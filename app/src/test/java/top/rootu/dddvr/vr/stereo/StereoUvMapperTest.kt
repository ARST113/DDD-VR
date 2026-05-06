package top.rootu.dddvr.vr.stereo

import org.junit.Assert.assertEquals
import org.junit.Test
import top.rootu.dddvr.vr.camera.Eye

class StereoUvMapperTest {
    @Test
    fun `mono uses full uv`() {
        val mapper = StereoUvMapper()
        mapper.stereoInputMode = StereoInputMode.MONO
        val uv = mapper.getUvTransform(Eye.LEFT)
        assertEquals(1f, uv.uScale)
        assertEquals(1f, uv.vScale)
    }

    @Test
    fun `sbs splits horizontal`() {
        val mapper = StereoUvMapper()
        mapper.stereoInputMode = StereoInputMode.SBS
        assertEquals(0f, mapper.getUvTransform(Eye.LEFT).uOffset)
        assertEquals(0.5f, mapper.getUvTransform(Eye.RIGHT).uOffset)
    }

    @Test
    fun `ou splits vertical and swap eyes flips mapping`() {
        val mapper = StereoUvMapper()
        mapper.stereoInputMode = StereoInputMode.OU
        mapper.swapEyes = true
        assertEquals(0.5f, mapper.getUvTransform(Eye.LEFT).vOffset)
        assertEquals(0f, mapper.getUvTransform(Eye.RIGHT).vOffset)
    }
}
