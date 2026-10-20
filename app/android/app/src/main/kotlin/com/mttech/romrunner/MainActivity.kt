package com.mttech.romrunner

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

/**
 * Hosts the "romrunner/switch" channel used to hand Switch games to an
 * installed emulator (Eden, Citron, ...). Games are picked with the system
 * document picker and RomRunner keeps persistent read access to them, so
 * multi-GB files are never copied. Launching sends ACTION_VIEW with a
 * one-time read grant for that file, which Yuzu-family emulators accept
 * (EmulationActivity has a VIEW filter for content:// octet-stream).
 */
class MainActivity : FlutterActivity() {
    private var pendingPick: MethodChannel.Result? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                "emulators" -> result.success(findEmulators())
                "pickGames" -> pickGames(result)
                "launch" -> launch(call.argument<String>("package")!!, call.argument<String>("uri")!!, result)
                "release" -> {
                    release(call.argument<String>("uri")!!)
                    result.success(null)
                }
                else -> result.notImplemented()
            }
        }
    }

    private fun gameIntent(uri: Uri) = Intent(Intent.ACTION_VIEW).apply {
        setDataAndType(uri, MIME)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        clipData = ClipData.newRawUri("game", uri)
    }

    /** Apps that open content:// game files and look like Switch emulators. */
    private fun findEmulators(): List<Map<String, String>> {
        val probe = Intent(Intent.ACTION_VIEW).setDataAndType(Uri.parse("content://romrunner.probe/game.nsp"), MIME)
        @Suppress("DEPRECATION")
        val handlers = packageManager.queryIntentActivities(probe, 0)
        return handlers
            .map { it.activityInfo.applicationInfo }
            .filter { it.packageName != packageName && HINTS.any { h -> it.packageName.contains(h, ignoreCase = true) } }
            .distinctBy { it.packageName }
            .map { mapOf("id" to it.packageName, "label" to packageManager.getApplicationLabel(it).toString()) }
    }

    private fun pickGames(result: MethodChannel.Result) {
        if (pendingPick != null) {
            result.error("busy", "The file picker is already open.", null)
            return
        }
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        }
        try {
            pendingPick = result
            @Suppress("DEPRECATION")
            startActivityForResult(intent, PICK_REQUEST)
        } catch (e: ActivityNotFoundException) {
            pendingPick = null
            result.error("no_picker", "No file picker is available on this device.", null)
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != PICK_REQUEST) return
        val result = pendingPick ?: return
        pendingPick = null
        if (resultCode != Activity.RESULT_OK || data == null) {
            result.success(emptyList<Map<String, String>>())
            return
        }
        val uris = mutableListOf<Uri>()
        data.clipData?.let { clip -> for (i in 0 until clip.itemCount) uris.add(clip.getItemAt(i).uri) }
        if (uris.isEmpty()) data.data?.let { uris.add(it) }
        val picked = uris.map { uri ->
            try {
                contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (_: SecurityException) {
                // Provider doesn't offer persistable grants; access lasts until reboot.
            }
            mapOf("uri" to uri.toString(), "name" to displayName(uri))
        }
        result.success(picked)
    }

    private fun displayName(uri: Uri): String {
        try {
            contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) c.getString(0)?.let { return it }
            }
        } catch (_: Exception) {
        }
        return uri.lastPathSegment?.substringAfterLast('/') ?: "game"
    }

    private fun launch(pkg: String, uriString: String, result: MethodChannel.Result) {
        val uri = Uri.parse(uriString)
        try {
            val intent = gameIntent(uri)
            if (pkg == CHOOSE_EACH_TIME) {
                startActivity(Intent.createChooser(intent, "Open Switch game with").apply {
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                })
            } else {
                intent.setPackage(pkg)
                startActivity(intent)
            }
            result.success(null)
        } catch (e: ActivityNotFoundException) {
            result.error("not_found", "That emulator isn't installed anymore. Choose another in Settings.", null)
        } catch (e: SecurityException) {
            result.error("no_access", "RomRunner lost access to this file. Remove it and add it again.", null)
        }
    }

    private fun release(uriString: String) {
        try {
            contentResolver.releasePersistableUriPermission(Uri.parse(uriString), Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (_: Exception) {
        }
    }

    companion object {
        private const val CHANNEL = "romrunner/switch"
        private const val MIME = "application/octet-stream"
        private const val PICK_REQUEST = 4201
        private const val CHOOSE_EACH_TIME = "*"
        private val HINTS = listOf("eden", "citron", "yuzu", "suyu", "sudachi", "kenji", "ryujinx", "ryubing", "torzu", "sumi")
    }
}
