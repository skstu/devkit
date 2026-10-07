#include <fstream>
#include <iostream>
#include <libwxui.hpp>
#include <wx/filename.h>

namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
class ServicesTest final : public wxui::Application {
  std::unique_ptr<wxui::DesktopWindow> window_;
  int failures_ = 0;
  bool OnAppInit() override {
    window_ = std::make_unique<wxui::DesktopWindow>(
        wxui::DesktopWindowSpec{"libwxui services"},
        "<Window><VerticalLayout name='fields'/></Window>");
    window_->Present();
    window_->Poster()([this] { run(); });
    return true;
  }
  void run() {
    try {
      const auto leap = wxui::ParseUtc("2024-02-29T23:59:59Z");
      check(leap && wxui::FormatUtc(*leap) == "2024-02-29T23:59:59Z",
            "UTC round trip");
      for (const auto *bad :
           {"2026-02-29T00:00:00Z", "2026-01-01T24:00:00Z",
            "2026-01-01T00:60:00Z", "2026-01-01T00:00:00+00:00",
            "2026-01-01T00:00:00Zgarbage"})
        check(!wxui::ParseUtc(bad), "reject malformed UTC dates");
      check(wxui::FormatUtc(*leap + std::chrono::seconds(1)) ==
                "2024-03-01T00:00:00Z",
            "UTC day boundary");
      check(wxui::FormatLocalTime("2026-03-08T06:59:59Z") ==
                "08/03/2026 01:59:59",
            "local time before DST transition");
      check(wxui::FormatLocalTime("2026-03-08T07:00:00.123Z") ==
                "08/03/2026 03:00:00",
            "fractional UTC and DST transition");
      check(wxui::FormatLocalTime("not a date") == "not a date",
            "invalid display stays unknown");
      check(wxui::ContainsIgnoringCase("Äbc 中文", "äBC"),
            "Unicode case insensitive search");
      const auto path = wxui::WxStringToUtf8(
          wxFileName::CreateTempFileName("wxui-services-"));
      check(wxui::WriteFileAtomically(path, "old") &&
                wxui::WriteFileAtomically(path, "new 中文"),
            "atomic UTF-8 replacement");
      std::ifstream file(path);
      const std::string bytes{std::istreambuf_iterator<char>(file), {}};
      file.close();
      wxRemoveFile(wxui::Utf8ToWxString(path));
      check(bytes == "new 中文", "saved bytes preserved");
      auto *fields = window_->Require<wxui::Container>("fields");
      auto edit = std::make_shared<wxui::FormEdit>();
      fields->Add(edit);
      window_->RefreshLayout();
      wxWeakRef<wxTextCtrl> retired = edit->native();
      check(bool(retired), "native edit created");
      fields->Remove(edit.get());
      edit.reset();
      check(!retired, "logical removal destroys native input");
      int ticks = 0;
      {
        wxui::Timer timer;
        window_->ShowDialog(
            "Timer lifetime",
            "<Window><Button name='close' text='Close'/></Window>", {240, 120},
            [&](auto &root) {
              auto *dialog = wxGetTopLevelParent(root.GetManager());
              timer.Start(20, [&, dialog] {
                ++ticks;
                timer.Stop();
                wxKeyEvent escape(wxEVT_CHAR_HOOK);
                escape.m_keyCode = WXK_ESCAPE;
                dialog->GetEventHandler()->ProcessEvent(escape);
              });
            });
      }
      check(ticks == 1,
            "timer dispatches once and stops inside nested modal loop");
      std::cout << "PASS: UTC/DST, atomic save, native input lifetime and "
                   "modal timer\n";
    } catch (const std::exception &e) {
      ++failures_;
      std::cerr << e.what() << '\n';
    }
    window_->FinishClose();
  }
  int OnRun() override {
    wxui::Application::OnRun();
    return failures_ ? 1 : 0;
  }
  int OnAppExit() override {
    window_.reset();
    return 0;
  }
};
} // namespace
WXUI_IMPLEMENT_APPLICATION(ServicesTest);
