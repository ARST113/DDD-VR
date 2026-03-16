package top.rootu.dddplayer.bridge

sealed class BridgeEvent {
    abstract val sessionId: String?
    abstract val ts: Long
    abstract val uri: String?

    data class SessionStarted(
        override val sessionId: String?,
        override val ts: Long,
        override val uri: String?,
        val title: String?,
        val playlistSize: Int,
        val startIndex: Int
    ) : BridgeEvent()

    data class PlaybackStateChanged(
        override val sessionId: String?,
        override val ts: Long,
        override val uri: String?,
        val isPlaying: Boolean,
        val isBuffering: Boolean,
        val position: Long?,
        val duration: Long?
    ) : BridgeEvent()

    data class PositionTick(
        override val sessionId: String?,
        override val ts: Long,
        override val uri: String?,
        val position: Long?,
        val duration: Long?,
        val bufferedPosition: Long?,
        val bufferedPercentage: Int?
    ) : BridgeEvent()

    data class SeekCompleted(
        override val sessionId: String?,
        override val ts: Long,
        override val uri: String?,
        val fromPosition: Long?,
        val toPosition: Long?
    ) : BridgeEvent()

    data class PlaylistItemChanged(
        override val sessionId: String?,
        override val ts: Long,
        override val uri: String?,
        val windowIndex: Int,
        val title: String?
    ) : BridgeEvent()

    data class SessionFinished(
        override val sessionId: String?,
        override val ts: Long,
        override val uri: String?,
        val position: Long?,
        val duration: Long?,
        val endBy: String
    ) : BridgeEvent()

    data class Error(
        override val sessionId: String?,
        override val ts: Long,
        override val uri: String?,
        val code: String?,
        val message: String?
    ) : BridgeEvent()

    data class UserAction(
        override val sessionId: String?,
        override val ts: Long,
        override val uri: String?,
        val action: String,
        val payload: Map<String, String> = emptyMap()
    ) : BridgeEvent()
}
