package top.rootu.dddvr.bridge

interface BridgeTransport {
    fun send(event: BridgeEvent)
}
