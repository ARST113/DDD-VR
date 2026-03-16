package top.rootu.dddplayer.bridge

class BridgeDispatcher(
    private val config: BridgeConfig,
    private val transport: BridgeTransport
) {
    fun emit(event: BridgeEvent) {
        if (!config.enabled) return
        transport.send(event)
    }
}
