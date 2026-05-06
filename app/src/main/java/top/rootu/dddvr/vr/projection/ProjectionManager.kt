package top.rootu.dddvr.vr.projection

import android.util.Log
import top.rootu.dddvr.vr.camera.Eye
import top.rootu.dddvr.vr.renderer.VideoTextureSource
import top.rootu.dddvr.vr.stereo.StereoUvMapper

class ProjectionManager(
    private val videoTextureSource: VideoTextureSource
) {
    private val projectionList = mutableMapOf<ProjectionType, Projection>()
    var currentProjectionType: ProjectionType = ProjectionType.FLAT
        private set

    fun hasProjection(type: ProjectionType): Boolean = projectionList.containsKey(type)

    fun getAvailableProjectionTypes(): Set<ProjectionType> = projectionList.keys

    fun register(type: ProjectionType, projection: Projection) {
        projectionList[type] = projection
        Log.i("DDDVR/Projection", "register projection=$type, available=${projectionList.keys}")
        applyVisibility()
    }

    fun setCurrentProjectionType(type: ProjectionType) {
        if (!hasProjection(type)) {
            Log.w("DDDVR/Projection", "failed switch projection=$type not registered; keeping $currentProjectionType")
            if (!hasProjection(currentProjectionType) && hasProjection(ProjectionType.FLAT)) {
                currentProjectionType = ProjectionType.FLAT
                Log.w("DDDVR/Projection", "fallback to FLAT due to invalid current projection")
            }
            return
        }
        currentProjectionType = type
        applyVisibility()
    }

    fun setAspectRatio(width: Int, height: Int) {
        projectionList.values.forEach { it.updateAspectRatio(width, height) }
    }

    fun setHeadMatrix(matrix: FloatArray) {
        projectionList.values.forEach { it.setHeadMatrix(matrix) }
    }

    fun renderEye(eye: Eye, stereoMapper: StereoUvMapper) {
        val current = projectionList[currentProjectionType]
        when {
            current != null -> current.render(eye, videoTextureSource, stereoMapper)
            projectionList[ProjectionType.FLAT] != null -> {
                Log.w("DDDVR/Projection", "missing current=$currentProjectionType render fallback to FLAT")
                projectionList[ProjectionType.FLAT]?.render(eye, videoTextureSource, stereoMapper)
            }
            else -> Log.e("DDDVR/Projection", "no projection available for render; black-screen prevented by skip")
        }
    }

    private fun applyVisibility() {
        projectionList.forEach { (type, projection) ->
            projection.setVisibility(type == currentProjectionType)
        }
    }
}
