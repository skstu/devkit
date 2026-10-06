#include <libwxui.hpp>

#include <wx/html/htmlwin.h>
#include <wx/log.h>

#include <fstream>
#include <iterator>

namespace wxui {

namespace {

std::string ReadFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

} // namespace

HtmlWindow::~HtmlWindow() {
    // htmlCtrl_ is a wxWindow child; wxWidgets owns the lifetime.
}

void HtmlWindow::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "src" || key == "source" || key == "file") {
        src_ = val;
        ReloadContent();
        return;
    }
    Control::SetAttribute(key, val);
    SyncNativeCtrl();
}

void HtmlWindow::OnManagerSet() {
    CreateNativeCtrl();
}

void HtmlWindow::CreateNativeCtrl() {
    if (htmlCtrl_ || !manager_) return;

    htmlCtrl_ = new wxHtmlWindow(manager_, wxID_ANY, rect_.GetTopLeft(),
                                 rect_.GetSize(),
                                 wxHW_SCROLLBAR_AUTO | wxBORDER_NONE);
    SyncNativeCtrl();
    ReloadContent();
}

void HtmlWindow::SyncNativeCtrl() {
    if (!htmlCtrl_) return;
    if (manager_) htmlCtrl_->SetFont(manager_->GetUIFont());
    if (bkColor_.IsOk() && bkColor_.Alpha() != 0) {
        htmlCtrl_->SetBackgroundColour(bkColor_);
    }
    htmlCtrl_->Enable(enabled_);
    htmlCtrl_->Show(IsNativeWindowVisible());
}

void HtmlWindow::SetRect(const wxRect& rc) {
    Control::SetRect(rc);
    if (htmlCtrl_) {
        htmlCtrl_->SetPosition(rc.GetTopLeft());
        htmlCtrl_->SetSize(rc.GetSize());
        htmlCtrl_->Show(IsNativeWindowVisible());
    }
}

void HtmlWindow::SetPage(std::string_view html) {
    text_.assign(html.data(), html.size());
    if (htmlCtrl_) {
        htmlCtrl_->SetPage(Utf8ToWxString(text_));
    }
}

bool HtmlWindow::LoadPage(const std::string& path) {
    src_ = path;
    if (src_.empty()) {
        SetPage({});
        return true;
    }

    std::string content;
    if (manager_) {
        manager_->LoadResourceBytes(src_, &content);
        if (content.empty()) {
            const std::string full = manager_->GetResourceRoot().empty()
                                         ? src_
                                         : manager_->GetResourceRoot() + "/" + src_;
            content = ReadFileBytes(full);
        }
    } else {
        content = ReadFileBytes(src_);
    }

    if (content.empty()) {
        wxLogWarning("libwxui: HtmlWindow cannot load '%s'", src_.c_str());
        return false;
    }
    SetPage(content);
    return true;
}

void HtmlWindow::ReloadContent() {
    if (!htmlCtrl_) return;
    if (!src_.empty()) {
        LoadPage(src_);
    } else if (!text_.empty()) {
        htmlCtrl_->SetPage(Utf8ToWxString(text_));
    }
}

void HtmlWindow::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Control::DoPaint(dc, clipRect);
}

} // namespace wxui
