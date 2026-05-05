package top.rootu.dddvr.bridge

import android.util.Log

class LocalStoreTransport(
    private val config: BridgeConfig
) : BridgeTransport {
    override fun send(event: BridgeEvent) {
        val type = event::class.simpleName ?: "Unknown"
        val envelope = BridgeEnvelope(
            schema = config.schemaVersion,
            type = type,
            client = config.client,
            sessionId = event.sessionId,
            ts = event.ts,
            payload = event
        )
        LocalBridgeStore.put(envelope)
        Log.d("DDDVRLocalBridge", "stored event=$type session=${event.sessionId}")
    }
}
