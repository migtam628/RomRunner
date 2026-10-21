import Cocoa
import FlutterMacOS

class MainFlutterWindow: NSWindow {
  private var iconChannel: FlutterMethodChannel?

  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    let windowFrame = self.frame
    self.contentViewController = flutterViewController
    self.setFrame(windowFrame, display: true)

    RegisterGeneratedPlugins(registry: flutterViewController)

    // Settings -> App icon: the Dock icon while RomRunner runs.
    let channel = FlutterMethodChannel(
      name: "romrunner/icon", binaryMessenger: flutterViewController.engine.binaryMessenger)
    channel.setMethodCallHandler { call, result in
      guard call.method == "setWindowIcon",
        let data = call.arguments as? FlutterStandardTypedData,
        let art = NSImage(data: data.data)
      else {
        result(FlutterMethodNotImplemented)
        return
      }
      // Inset like other Dock icons (Apple's grid leaves ~10% around the shape).
      let size = NSSize(width: 512, height: 512)
      let icon = NSImage(size: size, flipped: false) { rect in
        art.draw(in: rect.insetBy(dx: 50, dy: 50))
        return true
      }
      NSApp.applicationIconImage = icon
      result(nil)
    }
    iconChannel = channel

    super.awakeFromNib()
  }
}
