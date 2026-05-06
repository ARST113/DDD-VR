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
    fun `vr180 and vr360 produce different longitude spans`() {
        val mesh180 = SphereMeshFactory.createEquirect180()
        val mesh360 = SphereMeshFactory.createEquirect360()
        val minX180 = mesh180.vertices.filterIndexed { i, _ -> i % 3 == 0 }.minOrNull() ?: 0f
        val minX360 = mesh360.vertices.filterIndexed { i, _ -> i % 3 == 0 }.minOrNull() ?: 0f
        assertTrue(minX180 > minX360)
    }

    @Test
    fun `indices do not overflow vertices`() {
        val mesh = SphereMeshFactory.createEquirect360()
        val vertexCount = mesh.vertices.size / 3
        assertTrue(mesh.indices.all { it.toInt() in 0 until vertexCount })
    }
}
