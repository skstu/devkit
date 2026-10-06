#include <libwxui.hpp>

#if defined(__WXMSW__)
#  include <wx/msw/ole/activex.h>
#endif

namespace wxui {

// ══════════════════════════════════════════════════════════════════════════
// ActiveX
// ══════════════════════════════════════════════════════════════════════════

void ActiveX::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "clsid") { clsid_ = val; return; }
    Control::SetAttribute(key, val);
}

void ActiveX::DoPaint(wxDC& dc, const wxRect& clipRect) {
    // The native host (if any) paints itself; draw placeholder background.
    Control::DoPaint(dc, clipRect);
}

void ActiveX::SetRect(const wxRect& rc) {
    Control::SetRect(rc);
#if defined(__WXMSW__)
    if (nativeHost_) {
        nativeHost_->SetPosition(rc.GetTopLeft());
        nativeHost_->SetSize(rc.GetSize());
    }
#endif
}

void ActiveX::OnManagerSet() {
    if (clsid_.empty() || !manager_) return;
#if defined(__WXMSW__)
    if (!nativeHost_) {
        // Create an invisible placeholder window; full ActiveX hosting would
        // require wxActiveXContainer with a specific COM CLSID.
        nativeHost_ = new wxWindow(manager_, wxID_ANY,
                                   rect_.GetTopLeft(), rect_.GetSize());
    }
#endif
}

// ══════════════════════════════════════════════════════════════════════════
// WebBrowser
// ══════════════════════════════════════════════════════════════════════════

void WebBrowser::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "homepage" || key == "homePage") { homepage_ = val; return; }
    ActiveX::SetAttribute(key, val);
}

void WebBrowser::Navigate(const wxString& url) {
    (void)url;
    // Full implementation would call IWebBrowser2::Navigate via nativeHost_.
}

} // namespace wxui
