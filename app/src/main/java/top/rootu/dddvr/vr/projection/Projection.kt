package top.rootu.dddvr.vr.projection

import top.rootu.dddvr.vr.camera.Eye
import top.rootu.dddvr.vr.renderer.VideoTextureSource
import top.rootu.dddvr.vr.stereo.StereoUvMapper

abstract class Projection(
    val type: ProjectionType
) {
    var isVisible: Boolean = false
    var distance: Float = 5f
    var maxZoomIn: Float = 1f
    var maxZoomOut: Float = 30f
    var zoomStep: Float = 0.25f
    protected var headPoseMatrix: FloatArray = floatArrayOf(
        1f, 0f, 0f, 0f,
        0f, 1f, 0f, 0f,
        0f, 0f, 1f, 0f,
        0f, 0f, 0f, 1f
    )

    open fun setVisibility(visible: Boolean) {
        isVisible = visible
    }

    open fun zoomIn() {
        if (distance > maxZoomIn) distance -= zoomStep
    }

    open fun zoomOut() {
        if (distance < maxZoomOut) distance += zoomStep
    }

    open fun setHeadMatrix(matrix: FloatArray) {
        headPoseMatrix = matrix.copyOf()
    }

    abstract fun updateAspectRatio(width: Int, height: Int)
    abstract fun render(eye: Eye, texture: VideoTextureSource, stereoMapper: StereoUvMapper)
}
