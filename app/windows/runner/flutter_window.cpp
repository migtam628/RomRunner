#include "flutter_window.h"

#include <optional>

#include <flutter/standard_method_codec.h>

#include <vector>

#include "flutter/generated_plugin_registrant.h"

FlutterWindow::FlutterWindow(const flutter::DartProject& project)
    : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
  if (!Win32Window::OnCreate()) {
    return false;
  }

  RECT frame = GetClientArea();

  // The size here must match the window dimensions to avoid unnecessary surface
  // creation / destruction in the startup path.
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  // Ensure that basic setup of the controller was successful.
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    return false;
  }
  RegisterPlugins(flutter_controller_->engine());
  SetChildContent(flutter_controller_->view()->GetNativeWindow());

  icon_channel_ = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
      flutter_controller_->engine()->messenger(), "romrunner/icon",
      &flutter::StandardMethodCodec::GetInstance());
  icon_channel_->SetMethodCallHandler(
      [this](const flutter::MethodCall<flutter::EncodableValue>& call,
             std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
        if (call.method_name() != "setWindowIcon") {
          result->NotImplemented();
          return;
        }
        const auto* bytes = std::get_if<std::vector<uint8_t>>(call.arguments());
        if (bytes == nullptr || bytes->empty()) {
          result->Error("bad_args", "Expected PNG bytes");
          return;
        }
        // Windows Vista and later read PNG data as icon resource bits.
        HICON icon = CreateIconFromResourceEx(
            const_cast<PBYTE>(bytes->data()), static_cast<DWORD>(bytes->size()),
            TRUE, 0x00030000, 0, 0, LR_DEFAULTCOLOR);
        if (icon == nullptr) {
          result->Error("decode_failed", "Couldn't read the icon");
          return;
        }
        SendMessage(GetHandle(), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SendMessage(GetHandle(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        if (window_icon_ != nullptr) DestroyIcon(window_icon_);
        window_icon_ = icon;
        result->Success();
      });

  flutter_controller_->engine()->SetNextFrameCallback([&]() {
    this->Show();
  });

  // Flutter can complete the first frame before the "show window" callback is
  // registered. The following call ensures a frame is pending to ensure the
  // window is shown. It is a no-op if the first frame hasn't completed yet.
  flutter_controller_->ForceRedraw();

  return true;
}

void FlutterWindow::OnDestroy() {
  icon_channel_ = nullptr;
  if (window_icon_ != nullptr) {
    DestroyIcon(window_icon_);
    window_icon_ = nullptr;
  }
  if (flutter_controller_) {
    flutter_controller_ = nullptr;
  }

  Win32Window::OnDestroy();
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                              WPARAM const wparam,
                              LPARAM const lparam) noexcept {
  // Give Flutter, including plugins, an opportunity to handle window messages.
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  switch (message) {
    case WM_FONTCHANGE:
      flutter_controller_->engine()->ReloadSystemFonts();
      break;
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}
