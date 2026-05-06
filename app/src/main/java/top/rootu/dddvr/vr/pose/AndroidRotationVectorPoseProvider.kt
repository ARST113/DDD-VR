package top.rootu.dddvr.vr.pose

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.opengl.Matrix

class AndroidRotationVectorPoseProvider(context: Context) : HeadPoseProvider, SensorEventListener {
    private val sensorManager = context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
    private val rotationSensor = sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
    private val rawRotationMatrix = FloatArray(16)
    private val recenterMatrix = FloatArray(16)
    private val finalHeadMatrix = FloatArray(16)

    init {
        Matrix.setIdentityM(recenterMatrix, 0)
        Matrix.setIdentityM(finalHeadMatrix, 0)
    }

    override fun start() {
        rotationSensor?.let {
            sensorManager.registerListener(this, it, SensorManager.SENSOR_DELAY_GAME)
        }
    }

    override fun stop() {
        sensorManager.unregisterListener(this)
    }

    override fun recenter() {
        Matrix.invertM(recenterMatrix, 0, rawRotationMatrix, 0)
    }

    override fun getHeadMatrix(out: FloatArray) {
        System.arraycopy(finalHeadMatrix, 0, out, 0, 16)
    }

    override fun onSensorChanged(event: SensorEvent) {
        SensorManager.getRotationMatrixFromVector(rawRotationMatrix, event.values)
        Matrix.multiplyMM(finalHeadMatrix, 0, recenterMatrix, 0, rawRotationMatrix, 0)
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit
}
