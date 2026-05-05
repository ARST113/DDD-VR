package top.rootu.dddvr.data

import androidx.room.Entity
import androidx.room.PrimaryKey
import top.rootu.dddvr.model.StereoInputType
import top.rootu.dddvr.model.StereoOutputMode
import top.rootu.dddvr.renderer.StereoRenderer

@Entity(tableName = "video_settings")
data class VideoSettings(
    @PrimaryKey val uri: String,
    val lastUpdated: Long,
    val inputType: StereoInputType,
    val outputMode: StereoOutputMode,
    val anaglyphType: StereoRenderer.AnaglyphType,
    val swapEyes: Boolean,
    val depth: Int,
    val lastPosition: Long = 0L,
    val duration: Long = 0L,
    val audioTrackId: String? = null,
    val subtitleTrackId: String? = null
)