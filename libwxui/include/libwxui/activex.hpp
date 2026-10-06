#pragma once
/// libwxui — ActiveX host and WebBrowser controls.
/// On non-Windows platforms these degrade to stub containers.

#include "control.hpp"

namespace wxui {

// ── ActiveX ───────────────────────────────────────────────────────────────
class ActiveX : public Control {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "ActiveX"; }

    void SetRect(const wxRect& rc) override;
    void OnManagerSet()            override;

protected:
    std::string clsid_;

#if defined(__WXMSW__)
    wxWindow* nativeHost_ = nullptr;   ///< wxActiveXContainer on Windows
#endif
};

// ── WebBrowser ────────────────────────────────────────────────────────────
class WebBrowser : public ActiveX {
public:
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "WebBrowser"; }

    void Navigate(const wxString& url);
    [[nodiscard]] const std::string& GetHomePage() const { return homepage_; }

private:
    std::string homepage_;
};

} // namespace wxui
