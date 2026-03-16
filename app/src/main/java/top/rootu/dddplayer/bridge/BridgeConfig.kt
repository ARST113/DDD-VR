package top.rootu.dddplayer.bridge

data class BridgeConfig(
    val enabled: Boolean = false,
    val sessionId: String? = null,
    val mode: BridgeMode = BridgeMode.BROADCAST,
    val emitPosition: Boolean = true,
    val emitUserActions: Boolean = true,
    val positionIntervalMs: Long = 1000L,
    val client: String = "lampa"
)

enum class BridgeMode {
    BROADCAST
}
