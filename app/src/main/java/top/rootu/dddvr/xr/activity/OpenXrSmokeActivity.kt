package top.rootu.dddvr.xr.activity

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.util.Log

class OpenXrSmokeActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i("DDDVR/OpenXR", "OPENXR_SMOKE_ACTIVITY_START")
        val intentToPlayer = Intent(intent).apply {
            setClass(this@OpenXrSmokeActivity, OpenXrPlayerActivity::class.java)
            putExtra("openxr_smoke_only", true)
        }
        startActivity(intentToPlayer)
        finish()
    }
}
