package top.rootu.dddvr.vr.mesh

object SphereMeshFactory {
    fun createEquirect360(segmentsX: Int = 48, segmentsY: Int = 24): ProjectionMesh =
        createSphere(-180f, 180f, segmentsX, segmentsY)

    fun createEquirect180(segmentsX: Int = 48, segmentsY: Int = 24): ProjectionMesh =
        createSphere(-90f, 90f, segmentsX, segmentsY)

    private fun createSphere(minLonDeg: Float, maxLonDeg: Float, segX: Int, segY: Int): ProjectionMesh {
        val vertices = ArrayList<Float>()
        val uvs = ArrayList<Float>()
        val indices = ArrayList<Short>()

        for (y in 0..segY) {
            val v = y.toFloat() / segY
            val lat = ((v - 0.5f) * Math.PI).toFloat()
            val sinLat = kotlin.math.sin(lat)
            val cosLat = kotlin.math.cos(lat)
            for (x in 0..segX) {
                val u = x.toFloat() / segX
                val lon = Math.toRadians((minLonDeg + (maxLonDeg - minLonDeg) * u).toDouble()).toFloat()
                vertices += (kotlin.math.sin(lon) * cosLat)
                vertices += sinLat
                vertices += (-kotlin.math.cos(lon) * cosLat)
                uvs += u
                uvs += v
            }
        }

        val stride = segX + 1
        for (y in 0 until segY) {
            for (x in 0 until segX) {
                val i0 = (y * stride + x).toShort()
                val i1 = (i0 + 1).toShort()
                val i2 = ((y + 1) * stride + x).toShort()
                val i3 = (i2 + 1).toShort()
                indices += i0; indices += i2; indices += i1
                indices += i1; indices += i2; indices += i3
            }
        }

        return ProjectionMesh(vertices.toFloatArray(), uvs.toFloatArray(), indices.toShortArray())
    }
}
