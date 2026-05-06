package top.rootu.dddvr.vr.mesh

import org.junit.Assert.assertTrue
import org.junit.Test

class SphereMeshFactoryTest {
    @Test
    fun `mesh arrays are non-empty`() {
        val mesh = SphereMeshFactory.createEquirect360()
        assertTrue(mesh.vertices.isNotEmpty())
        assertTrue(mesh.texCoords.isNotEmpty())
        assertTrue(mesh.indices.isNotEmpty())
    }

    @Test
    fun `vr180 and vr360 meshes are valid`() {
        val mesh180 = SphereMeshFactory.createEquirect180()
        val mesh360 = SphereMeshFactory.createEquirect360()

        assertTrue(mesh180.vertices.isNotEmpty())
        assertTrue(mesh180.texCoords.isNotEmpty())
        assertTrue(mesh180.indices.isNotEmpty())

        assertTrue(mesh360.vertices.isNotEmpty())
        assertTrue(mesh360.texCoords.isNotEmpty())
        assertTrue(mesh360.indices.isNotEmpty())
    }

    @Test
    fun `indices do not overflow vertices`() {
        val mesh = SphereMeshFactory.createEquirect360()
        val vertexCount = mesh.vertices.size / 3
        assertTrue(mesh.indices.all { it.toInt() in 0 until vertexCount })
    }
}
