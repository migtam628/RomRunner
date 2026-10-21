import Flutter
import UIKit

@main
@objc class AppDelegate: FlutterAppDelegate, FlutterImplicitEngineDelegate {
  private var iconChannel: FlutterMethodChannel?

  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }

  func didInitializeImplicitFlutterEngine(_ engineBridge: FlutterImplicitEngineBridge) {
    GeneratedPluginRegistrant.register(with: engineBridge.pluginRegistry)

    // Settings -> App icon. Alternate icon sets are AppIcon-<Name> in
    // Assets.xcassets (ASSETCATALOG_COMPILER_ALTERNATE_APPICON_NAMES).
    guard let registrar = engineBridge.pluginRegistry.registrar(forPlugin: "RomRunnerAppIcon") else { return }
    let channel = FlutterMethodChannel(name: "romrunner/icon", binaryMessenger: registrar.messenger())
    channel.setMethodCallHandler { call, result in
      guard call.method == "setIcon" else {
        result(FlutterMethodNotImplemented)
        return
      }
      let name = (call.arguments as? [String: Any])?["name"] as? String
      guard UIApplication.shared.supportsAlternateIcons else {
        result(FlutterError(code: "unsupported", message: "This device can't change app icons", details: nil))
        return
      }
      UIApplication.shared.setAlternateIconName(name) { error in
        if let error = error {
          result(FlutterError(code: "failed", message: error.localizedDescription, details: nil))
        } else {
          result(nil)
        }
      }
    }
    iconChannel = channel
  }
}
