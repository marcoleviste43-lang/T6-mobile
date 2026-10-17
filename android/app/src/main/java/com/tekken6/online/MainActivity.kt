package com.tekken6.online

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

/** PROTOTYPE: diagnostic lobby client for the backend. Not the game, no PSP runtime. */
class MainActivity : Activity() {
    private val io = Executors.newSingleThreadExecutor()
    private val ui = Handler(Looper.getMainLooper())
    private var token: String? = null
    private var myId: String? = null
    private var matchId: String? = null
    private var opponentId: String? = null
    private val prefs by lazy { getSharedPreferences("t6", MODE_PRIVATE) }
    private lateinit var status: TextView
    private lateinit var url: EditText
    private lateinit var name: EditText
    private lateinit var code: EditText

    private val poller = object : Runnable {
        override fun run() {
            if (token != null) call("GET", "/state", null) { show(it) }
            ui.postDelayed(this, 1500)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(32, 48, 32, 32) }
        col.addView(TextView(this).apply { text = "TEKKEN 6 NATIVE ONLINE\nlobby prototype (no game runtime yet)"; textSize = 18f })
        url = EditText(this).apply { setText(prefs.getString("url", "http://127.0.0.1:8080")); hint = "server URL"; inputType = InputType.TYPE_TEXT_VARIATION_URI }
        name = EditText(this).apply { setText("player" + (100..999).random()); hint = "display name (3-20 chars)" }
        code = EditText(this).apply { hint = "room code"; inputType = InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS }
        col.addView(url); col.addView(name)
        col.addView(btn("Health check") { call("GET", "/health", null, auth = false) { show(it) } })
        col.addView(btn("Connect as guest") { connect() })
        col.addView(btn("Casual queue") { post("/matchmaking/join", JSONObject().put("mode", "casual")) })
        col.addView(btn("Ranked queue") { post("/matchmaking/join", JSONObject().put("mode", "ranked")) })
        col.addView(btn("Cancel queue") { post("/matchmaking/cancel") })
        col.addView(btn("Create private room") { post("/rooms") })
        col.addView(code)
        col.addView(btn("Join room") { post("/rooms/join", JSONObject().put("code", code.text.toString().trim().uppercase())) })
        col.addView(btn("Ready") { post("/rooms/ready", JSONObject().put("ready", true)) })
        col.addView(btn("Not ready") { post("/rooms/ready", JSONObject().put("ready", false)) })
        col.addView(btn("Leave room") { post("/rooms/leave") })
        col.addView(btn("Match loaded") { post("/matches/loaded") })
        col.addView(btn("Report: I won") { report(myId) })
        col.addView(btn("Report: opponent won") { report(opponentId) })
        col.addView(btn("Report: draw") { report("draw") })
        col.addView(btn("Match history") { call("GET", "/matches/history", null) { show(it) } })
        status = TextView(this).apply { text = "not connected"; setTextIsSelectable(true); typeface = android.graphics.Typeface.MONOSPACE }
        col.addView(status)
        setContentView(ScrollView(this).apply { addView(col) })
        ui.postDelayed(poller, 1500)
    }

    override fun onPause() { prefs.edit().putString("url", url.text.toString().trim()).apply(); super.onPause() }

    override fun onDestroy() { ui.removeCallbacks(poller); io.shutdown(); super.onDestroy() }

    private fun btn(label: String, onClick: () -> Unit) = Button(this).apply { text = label; setOnClickListener { onClick() } }

    private fun show(r: Pair<Int, String>) {
        if (r.first == 401) token = null
        if (r.first == 200) {
            try {
                val room = JSONObject(r.second).optJSONObject("room")
                matchId = room?.optString("matchId")?.takeIf { it.isNotEmpty() }
                opponentId = null
                val ps = room?.optJSONArray("players")
                if (ps != null) for (i in 0 until ps.length()) {
                    val id = ps.getJSONObject(i).getString("playerId")
                    if (id != myId) opponentId = id
                }
            } catch (_: Exception) { /* not a state response */ }
        }
        status.text = "HTTP ${r.first}\n${r.second}"
    }

    private fun connect() {
        call("POST", "/auth/guest", JSONObject().put("displayName", name.text.toString().trim()), auth = false) { r ->
            if (r.first == 200) {
                val j = JSONObject(r.second)
                token = j.getString("token"); myId = j.getString("playerId")
            }
            show(r)
        }
    }

    private fun report(winner: String?) {
        val m = matchId
        if (m == null || winner == null) { status.text = "no active match or opponent yet"; return }
        post("/matches/result", JSONObject().put("matchId", m).put("winnerId", winner))
    }

    private fun post(path: String, body: JSONObject? = JSONObject()) = call("POST", path, body) { show(it) }

    private fun call(method: String, path: String, body: JSONObject?, auth: Boolean = true, cb: (Pair<Int, String>) -> Unit) {
        val base = url.text.toString().trim().trimEnd('/')
        val tok = token
        io.execute {
            val result = try {
                val c = URL("$base/api/v1$path").openConnection() as HttpURLConnection
                c.requestMethod = method; c.connectTimeout = 4000; c.readTimeout = 4000
                if (auth && tok != null) c.setRequestProperty("Authorization", "Bearer $tok")
                if (method == "POST") {
                    c.doOutput = true; c.setRequestProperty("Content-Type", "application/json")
                    c.outputStream.use { it.write((body ?: JSONObject()).toString().toByteArray()) }
                }
                val code = c.responseCode
                val text = (if (code >= 400) c.errorStream else c.inputStream)?.bufferedReader()?.readText() ?: ""
                code to text
            } catch (e: Exception) { -1 to "ERROR: ${e.message}" }
            ui.post { cb(result) }
        }
    }
}
