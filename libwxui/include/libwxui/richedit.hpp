#pragma once
/// libwxui — RichEdit control with a shared styled editor and drawn scrollbars.

#include "container.hpp"
#include <wx/colour.h>
#include <wx/textctrl.h>

namespace wxui {
class TextEditor;

class RichEdit : public Container {
public:
    ~RichEdit() override;
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "RichEdit"; }

    void SetRect(const wxRect& rc) override;
    void OnManagerSet()            override;

    // ── Content ──────────────────────────────────────────────────────
    [[nodiscard]] wxString GetValue()       const;
    [[nodiscard]] std::string GetValueUtf8() const { return WxStringToUtf8(GetValue()); }
    void SetValue(const wxString& v);
    void SetValue(std::string_view v) { SetValue(Utf8ToWxString(v)); }
    void SetValueUtf8(std::string_view v) { SetValue(Utf8ToWxString(v)); }
    // Retain at most maxCharacters Unicode characters, including new text.
    void AppendBounded(std::string_view text, std::size_t maxCharacters);
    void AppendText(const wxString& t);
    void AppendText(std::string_view t) { AppendText(Utf8ToWxString(t)); }
    void AppendText(const wxString& t, const wxColour& color);
    void AppendText(std::string_view t, const wxColour& color) {
        AppendText(Utf8ToWxString(t), color);
    }
    void Clear();

    // ── Style ─────────────────────────────────────────────────────────
    void SetReadOnly(bool r);

private:
    void CreateNativeCtrl();
    void SyncStyle();

    TextEditor* textCtrl_       = nullptr;

    bool borderVisible_  = false;
    bool autoVScroll_    = true;
    bool autoHScroll_    = false;
    bool wantTab_        = true;
    bool wantReturn_     = true;
    bool wantCtrlReturn_ = true;
    bool transparent_    = true;
    bool rich_           = false;
    bool multiLine_      = true;
    bool readOnly_       = false;
    bool password_       = false;
    wxColour textColor_;
    wxRect textPadding_  {6, 0, 6, 0};
    int fontId_          = -1;
    bool monospace_      = false;
};

} // namespace wxui
