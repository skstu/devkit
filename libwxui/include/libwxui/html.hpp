#pragma once
/// libwxui HtmlWindow control.

#include "control.hpp"

#include <string_view>

class wxHtmlWindow;

namespace wxui {

class HtmlWindow : public Control {
public:
    ~HtmlWindow() override;
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "HtmlWindow"; }

    void SetRect(const wxRect& rc) override;
    void OnManagerSet() override;

    void SetPage(std::string_view html);
    bool LoadPage(const std::string& path);

private:
    void CreateNativeCtrl();
    void SyncNativeCtrl();
    void ReloadContent();

    wxHtmlWindow* htmlCtrl_ = nullptr;
    std::string src_;
};

} // namespace wxui
