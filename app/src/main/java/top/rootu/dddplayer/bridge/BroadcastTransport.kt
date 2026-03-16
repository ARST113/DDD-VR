package top.rootu.dddplayer.bridge

import android.content.Context
import android.content.Intent
import com.google.gson.Gson

class BroadcastTransport(
    private val context: Context
) : BridgeTransport {

    private val gson = Gson()

    override fun send(event: BridgeEvent) {
        val intent = Intent(ACTION_EVENT)
        intent.putExtra(EXTRA_EVENT_TYPE, event::class.simpleName)
        intent.putExtra(EXTRA_EVENT_JSON, gson.toJson(event))
        context.sendBroadcast(intent)
    }

    companion object {
        const val ACTION_EVENT = "top.rootu.dddplayer.bridge.EVENT"
        const val EXTRA_EVENT_TYPE = "event_type"
        const val EXTRA_EVENT_JSON = "event_json"
    }
}
