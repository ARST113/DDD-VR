package top.rootu.dddvr.vr.pose

interface HeadPoseProvider {
    fun start()
    fun stop()
    fun recenter()
    fun getHeadMatrix(out: FloatArray)
}
