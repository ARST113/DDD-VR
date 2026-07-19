package top.rootu.dddvr.xr.ui

import org.junit.Assert.assertSame
import org.junit.Test

class OpenXrPlayerUiActionTest {
    @Test
    fun nativeCloseModalCodeMapsToCloseModal() {
        val action = OpenXrPlayerUiAction.fromNative(
            actionType = 105,
            intValue = 0,
            floatValue = 0f,
            stringValue = null
        )

        assertSame(OpenXrPlayerUiAction.CloseModal, action)
    }
}
