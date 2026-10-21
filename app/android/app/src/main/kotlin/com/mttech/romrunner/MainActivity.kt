package com.mttech.romrunner

import android.Manifest
import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.ComponentName
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.OpenableColumns
import android.provider.Settings
import androidx.core.content.FileProvider
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

/**
 * "romrunner/external" hands games for systems RomRunner doesn't emulate
 * (PS2, Switch, GameCube, PSP ...) to the emulator app the user installed.
 * The intent for each emulator (component, action, extras) comes from the
 * Dart catalog (external_catalog.dart, derived from ES-DE). Games are
 * either picked with the system document picker (RomRunner keeps
 * persistent read access, so multi-GB files are never copied) or found in
 * game folders and shared through GameFileProvider. Either way the
 * emulator gets a one-time read grant for that one file.
 *
 * "romrunner/storage" asks for the access needed to scan game folders.
 */
class MainActivity : FlutterActivity() {
    private var pendingPick: MethodChannel.Result? = null
    private var pendingAccess: MethodChannel.Result? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, ICON_CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                "setIcon" -> {
                    val alias = call.argument<String>("alias")
                    val all = call.argument<List<String>>("all") ?: emptyList()
                    if (alias == null || alias !in all) {
                        result.error("bad_args", "Unknown icon", null)
                    } else {
                        try {
                            setLauncherIcon(alias, all)
                            result.success(null)
                        } catch (e: Exception) {
                            result.error("failed", e.message, null)
                        }
                    }
                }
                else -> result.notImplemented()
            }
        }
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                "installed" -> result.success(installed(call.argument<List<String>>("packages") ?: emptyList()))
                "switchHandlers" -> result.success(findSwitchHandlers())
                "pickGames" -> pickGames(result)
                "launch" -> launch(call, result)
                "installApk" -> installApk(call.argument<String>("path")!!, result)
                "release" -> {
                    release(call.argument<String>("uri")!!)
                    result.success(null)
                }
                else -> result.notImplemented()
            }
        }
        // Reading game folders in place.
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, STORAGE_CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                "hasAccess" -> result.success(hasStorageAccess())
                "requestAccess" -> requestStorageAccess(result)
                else -> result.notImplemented()
            }
        }
    }

    private fun hasStorageAccess(): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Environment.isExternalStorageManager()
        } else {
            checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
        }

    private fun requestStorageAccess(result: MethodChannel.Result) {
        if (hasStorageAccess()) {
            result.success(true)
            return
        }
        pendingAccess = result
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Settings page with the "Allow access to manage all files" switch.
            val perApp = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName"))
            val general = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
            for (intent in listOf(perApp, general)) {
                try {
                    @Suppress("DEPRECATION")
                    startActivityForResult(intent, ACCESS_REQUEST)
                    return
                } catch (_: ActivityNotFoundException) {
                }
            }
            pendingAccess = null
            result.success(false) // e.g. a TV build without that settings page
        } else {
            requestPermissions(arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE), ACCESS_REQUEST)
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == ACCESS_REQUEST) {
            pendingAccess?.success(hasStorageAccess())
            pendingAccess = null
        }
    }

    /** Which of [packages] are installed, with their labels. */
    private fun installed(packages: List<String>): List<Map<String, String>> = packages.distinct().mapNotNull { pkg ->
        try {
            val info = packageManager.getApplicationInfo(pkg, 0)
            mapOf("id" to pkg, "label" to packageManager.getApplicationLabel(info).toString())
        } catch (_: PackageManager.NameNotFoundException) {
            null
        }
    }

    /** Apps that open content:// game files and look like Switch emulators. */
    private fun findSwitchHandlers(): List<Map<String, String>> {
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
        if (requestCode == ACCESS_REQUEST) {
            pendingAccess?.success(hasStorageAccess())
            pendingAccess = null
            return
        }
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

    /**
     * Starts an emulator with a game. Arguments: "spec" (from AndroidLaunch
     * in Dart), plus "uri" (content:// from the picker) or "path" (a file or
     * folder in a game folder), plus optional "inject" (Vita3K title id).
     */
    @Suppress("UNCHECKED_CAST")
    private fun launch(call: io.flutter.plugin.common.MethodCall, result: MethodChannel.Result) {
        val spec = call.argument<Map<String, Any?>>("spec")!!
        val pkg = spec["package"] as String
        val path = call.argument<String>("path")
        val isDir = path != null && java.io.File(path).isDirectory
        val uri: Uri? = try {
            when {
                call.argument<String>("uri") != null -> Uri.parse(call.argument<String>("uri"))
                path != null && !isDir -> FileProvider.getUriForFile(this, "$packageName.games", java.io.File(path))
                else -> null
            }
        } catch (e: IllegalArgumentException) {
            result.error("no_access", "RomRunner can't share $path.", null)
            return
        }
        val wantsData = spec["data"] as Boolean? ?: true
        val intent = Intent(spec["action"] as String? ?: if (wantsData) Intent.ACTION_VIEW else Intent.ACTION_MAIN)
        (spec["category"] as String?)?.let { intent.addCategory(it) }
        if (uri != null) {
            if (wantsData) intent.setDataAndType(uri, MIME)
            (spec["extra"] as String?)?.let { intent.putExtra(it, uri.toString()) }
            intent.clipData = ClipData.newRawUri("game", uri)
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        if (isDir) (spec["dirExtra"] as String?)?.let { intent.putExtra(it, path) }
        (spec["extraArray"] as String?)?.let { key ->
            call.argument<String>("inject")?.let { intent.putExtra(key, arrayOf("-r", it)) }
        }
        if (spec["clearTask"] == true) {
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
        }
        try {
            if (pkg == CHOOSE_EACH_TIME) {
                startActivity(Intent.createChooser(intent, "Open game with").apply {
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                })
            } else {
                val activity = spec["activity"] as String?
                if (activity != null) {
                    intent.component = ComponentName(pkg, if (activity.startsWith(".")) pkg + activity else activity)
                } else {
                    intent.setPackage(pkg)
                }
                startActivity(intent)
            }
            result.success(null)
        } catch (e: ActivityNotFoundException) {
            result.error("not_found", "That emulator isn't installed anymore. Choose another in Settings.", null)
        } catch (e: SecurityException) {
            result.error("no_access", "RomRunner lost access to this file, or the emulator refused it. Remove the game and add it again.", null)
        }
    }

    /**
     * Opens Android's package installer for an emulator APK RomRunner
     * downloaded from the emulator's official releases. The user confirms.
     */
    private fun installApk(path: String, result: MethodChannel.Result) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && !packageManager.canRequestPackageInstalls()) {
            // First time: the user has to allow RomRunner to install apps.
            try {
                startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:$packageName")))
            } catch (_: ActivityNotFoundException) {
            }
            result.error("needs_permission", "Allow RomRunner to install apps, then press Install again.", null)
            return
        }
        val uri = FileProvider.getUriForFile(this, "$packageName.games", java.io.File(path))
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/vnd.android.package-archive")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        try {
            startActivity(intent)
            result.success(null)
        } catch (e: ActivityNotFoundException) {
            result.error("no_installer", "This device has no package installer.", null)
        }
    }

    private fun release(uriString: String) {
        try {
            contentResolver.releasePersistableUriPermission(Uri.parse(uriString), Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (_: Exception) {
        }
    }

    /**
     * Each icon is an <activity-alias> of MainActivity in the manifest. Turn the
     * chosen one on first, then the others off, so there is always a launcher
     * entry.
     */
    private fun setLauncherIcon(alias: String, all: List<String>) {
        val base = MainActivity::class.java.`package`!!.name
        val order = listOf(alias) + all.filter { it != alias }
        for (a in order) {
            val state = if (a == alias) PackageManager.COMPONENT_ENABLED_STATE_ENABLED
            else PackageManager.COMPONENT_ENABLED_STATE_DISABLED
            packageManager.setComponentEnabledSetting(ComponentName(this, base + a), state, PackageManager.DONT_KILL_APP)
        }
    }

    companion object {
        private const val ICON_CHANNEL = "romrunner/icon"
        private const val CHANNEL = "romrunner/external"
        private const val STORAGE_CHANNEL = "romrunner/storage"
        private const val ACCESS_REQUEST = 4202
        private const val MIME = "application/octet-stream"
        private const val PICK_REQUEST = 4201
        private const val CHOOSE_EACH_TIME = "*"
        private val HINTS = listOf("eden", "citron", "yuzu", "suyu", "sudachi", "kenji", "ryujinx", "ryubing", "torzu", "sumi")
    }
}

/** Own subclass so it can't clash with FileProviders declared by plugins. */
class GameFileProvider : FileProvider()
