package top.rootu.dddvr.bridge

import android.util.Log

class CompositeTransport(
    private val transports: List<BridgeTransport>
) : BridgeTransport {
    override fun send(event: BridgeEvent) {
        transports.forEach { transport ->
            try {
                transport.send(event)
            } catch (e: Exception) {
                Log.w(
                    "DDDVRBridge",
                    "transport failed=${transport::class.simpleName}, event=${event::class.simpleName}",
                    e
                )
            }
        }
    }
}
