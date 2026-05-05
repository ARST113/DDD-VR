package top.rootu.dddvr.vr.projection

import top.rootu.dddvr.vr.camera.Eye
import top.rootu.dddvr.vr.renderer.VideoTextureSource
import top.rootu.dddvr.vr.stereo.StereoUvMapper

class ProjectionManager(
    private val videoTextureSource: VideoTextureSource
) {
    private val projectionList = mutableMapOf<ProjectionType, Projection>()
    private var currentProjectionType: ProjectionType = ProjectionType.FLAT

    fun register(type: ProjectionType, projection: Projection) {
        projectionList[type] = projection
        applyVisibility()
    }

    fun setCurrentProjectionType(type: ProjectionType) {
        currentProjectionType = type
        applyVisibility()
    }

    fun zoomIn() {
        projectionList[currentProjectionType]?.zoomIn()
    }

    fun zoomOut() {
        projectionList[currentProjectionType]?.zoomOut()
    }

    fun setAspectRatio(width: Int, height: Int) {
        projectionList.values.forEach { it.updateAspectRatio(width, height) }
    }

    fun renderEye(eye: Eye, stereoMapper: StereoUvMapper) {
        projectionList[currentProjectionType]?.render(eye, videoTextureSource, stereoMapper)
    }

    private fun applyVisibility() {
        projectionList.forEach { (type, projection) ->
            projection.setVisibility(type == currentProjectionType)
        }
    }
}
