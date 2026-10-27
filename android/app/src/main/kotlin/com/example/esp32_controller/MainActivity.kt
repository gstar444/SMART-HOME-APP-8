package com.example.esp32_controller

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Intent
import android.speech.RecognizerIntent
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

/**
 * Opens Android's own speech dialog for the dashboard's "Voice Assistant" button.
 * Dart side: lib/voice.dart (MethodChannel "esp32_controller/voice", method "listen").
 */
class MainActivity : FlutterActivity() {
    private val voiceRequest = 7001
    private var pending: MethodChannel.Result? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "esp32_controller/voice")
            .setMethodCallHandler { call, result ->
                if (call.method != "listen") {
                    result.notImplemented()
                    return@setMethodCallHandler
                }
                if (pending != null) {
                    result.error("busy", "Already listening", null)
                    return@setMethodCallHandler
                }
                val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
                    putExtra(
                        RecognizerIntent.EXTRA_LANGUAGE_MODEL,
                        RecognizerIntent.LANGUAGE_MODEL_FREE_FORM
                    )
                    putExtra(RecognizerIntent.EXTRA_LANGUAGE, call.argument<String>("locale") ?: "en-IN")
                    putExtra(RecognizerIntent.EXTRA_PROMPT, call.argument<String>("prompt") ?: "")
                    putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1)
                }
                pending = result
                try {
                    startActivityForResult(intent, voiceRequest)
                } catch (e: ActivityNotFoundException) {
                    pending = null
                    result.error("unavailable", "No speech recognizer on this phone", null)
                }
            }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (requestCode == voiceRequest) {
            val result = pending
            pending = null
            if (resultCode == Activity.RESULT_OK) {
                val words = data?.getStringArrayListExtra(RecognizerIntent.EXTRA_RESULTS)
                result?.success(words?.firstOrNull())
            } else {
                result?.success(null)
            }
            return
        }
        super.onActivityResult(requestCode, resultCode, data)
    }
}
