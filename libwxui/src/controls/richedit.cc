#include <libwxui.hpp>
#include <libwxui/text_editor.hpp>
#include <libwxui/appearance.hpp>

#include <algorithm>
#include <wx/log.h>
#include <wx/textctrl.h>

namespace wxui {

namespace {

wxColour ResolveRichTextColor(UIManager* manager, const wxColour& requested) {
    if (requested.IsOk()) return requested;
    if (manager && manager->GetRoot()) return manager->GetRoot()->GetDefaultFontColor();
    return *wxBLACK;
}

void BindTextCtrlEditShortcuts(TextEditor* textCtrl) {
    if (!textCtrl) return;

    textCtrl->Bind(wxEVT_KEY_DOWN, [textCtrl](wxKeyEvent& event) {
        const bool accelerator =
            (event.ControlDown() || event.CmdDown()) && !event.AltDown();
        if (!accelerator) {
            event.Skip();
            return;
        }

        int key = event.GetKeyCode();
        if (key >= 'a' && key <= 'z') key = key - 'a' + 'A';

        switch (key) {
            case 'A':
                textCtrl->SetSelection(-1, -1);
                return;
            case 'C':
                textCtrl->Copy();
                return;
            case 'V':
                if (textCtrl->IsEditable()) textCtrl->Paste();
                return;
            default:
                event.Skip();
                return;
        }
    });
}

} // namespace

RichEdit::~RichEdit() {
    // nativeCtrl_ is a wxWindow child; wxWidgets owns the lifetime
}

void RichEdit::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "monospace") { monospace_ = ParseBOOL(val); SyncStyle(); return; }
    if (key == "bordervisible"||key=="borderVisible") { borderVisible_=ParseBOOL(val); return; }
    if (key == "autovscroll"  ||key=="autoVScroll")   { autoVScroll_  =ParseBOOL(val); return; }
    if (key == "autohscroll"  ||key=="autoHScroll")   { autoHScroll_  =ParseBOOL(val); return; }
    if (key == "wanttab"      ||key=="wantTab")       { wantTab_      =ParseBOOL(val); return; }
    if (key == "wantreturn"   ||key=="wantReturn")    { wantReturn_   =ParseBOOL(val); return; }
    if (key == "wantctrlreturn"||key=="wantCtrlReturn"){ wantCtrlReturn_=ParseBOOL(val);return; }
    if (key == "transparent")                         { transparent_  =ParseBOOL(val); return; }
    if (key == "rich")                                { rich_         =ParseBOOL(val); return; }
    if (key == "multiline"    ||key=="multiLine")     { multiLine_    =ParseBOOL(val); return; }
    if (key == "readonly"     ||key=="readOnly")      { readOnly_     =ParseBOOL(val); if (textCtrl_) textCtrl_->SetEditable(!readOnly_); return; }
    if (key == "password")                            { password_     =ParseBOOL(val); return; }
    if (key == "textcolor")                           { textColor_    =ParseDWORD(val); SyncStyle(); return; }
    if (key == "textpadding"  ||key=="textPadding")   { textPadding_  =ParseRECT(val); if (textCtrl_) SetRect(rect_); return; }
    if (key == "font")                                { fontId_       =ParseINT(val, -1); SyncStyle(); return; }
    Container::SetAttribute(key, val);
    if (textCtrl_) {
        if (key == "text") textCtrl_->SetValue(Utf8ToWxString(text_));
        SyncStyle();
    }
}

void RichEdit::OnManagerSet() {
    CreateNativeCtrl();
}

void RichEdit::CreateNativeCtrl() {
    if (textCtrl_ || !manager_) return;

    long style = wxBORDER_NONE;
    if (wantReturn_) style |= wxTE_PROCESS_ENTER;
    if (wantTab_) style |= wxTE_PROCESS_TAB;
    if (multiLine_) {
        style |= wxTE_MULTILINE;
        style |= autoVScroll_ ? wxVSCROLL : wxTE_NO_VSCROLL;
        if (autoHScroll_) style |= wxHSCROLL | wxTE_DONTWRAP;
    }
    if (password_)   style |= wxTE_PASSWORD;
    if (readOnly_)   style |= wxTE_READONLY;
    if (rich_)       style |= wxTE_RICH2;

    textCtrl_ = new TextEditor(manager_, wxID_ANY,
                               Utf8ToWxString(text_), rect_.GetTopLeft(),
                               rect_.GetSize(), style);
    BindTextCtrlEditShortcuts(textCtrl_);
    textCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        event.Skip();
        if (textCtrl_) {
            text_ = WxStringToUtf8(textCtrl_->GetValue());
        }
        FireNotify("valuechanged");
    });
    SetRect(rect_);
    SyncStyle();
}

void RichEdit::SyncStyle() {
    if (!textCtrl_) return;
    textCtrl_->SetToolTip(Utf8ToWxString(tooltip_));
    if (manager_) {
        auto font = manager_->GetUIFont();
        if (monospace_) font = CodeFont();
        textCtrl_->SetFont(font);
    }
    if (bkColor_.IsOk() && bkColor_.Alpha() != 0) {
        textCtrl_->SetOwnBackgroundColour(bkColor_);
    }
    const wxColour textColor = ResolveRichTextColor(manager_, textColor_);
    textCtrl_->SetOwnForegroundColour(textColor);
    wxTextAttr textStyle;
    textStyle.SetTextColour(textColor);
    textCtrl_->SetDefaultStyle(textStyle);
    if (rich_) {
        textCtrl_->SetStyle(0, textCtrl_->GetLastPosition(), textStyle);
    }
    textCtrl_->Enable(enabled_);
    textCtrl_->Show(IsNativeWindowVisible());
}

void RichEdit::SetRect(const wxRect& rc) {
    Control::SetRect(rc);
    if (textCtrl_) {
        const int roundInset = borderRound_.x > 0
            ? std::max(1, std::min(4, borderRound_.x / 2))
            : 0;
        const int inset = std::max(borderSize_ > 0 ? borderSize_ : 0,
                                   roundInset);
        const int leftInset = inset + std::max(0, textPadding_.x);
        const int topInset = inset + std::max(0, textPadding_.y);
        const int rightInset = inset + std::max(0, textPadding_.width);
        const int bottomInset = inset + std::max(0, textPadding_.height);
        const wxRect nativeRect(rc.x + leftInset, rc.y + topInset,
                                std::max(0, rc.width - leftInset - rightInset),
                                std::max(0, rc.height - topInset - bottomInset));
        if (textCtrl_->GetRect() != nativeRect) textCtrl_->SetSize(nativeRect);
        textCtrl_->Show(IsNativeWindowVisible());
    }
}

wxString RichEdit::GetValue() const {
    return textCtrl_ ? textCtrl_->GetValue() : Utf8ToWxString(text_);
}

void RichEdit::SetValue(const wxString& v) {
    text_ = WxStringToUtf8(v);
    if (textCtrl_) {
        const bool wasReadOnly = readOnly_;
        if (wasReadOnly) textCtrl_->SetEditable(true);
        const wxColour textColor = ResolveRichTextColor(manager_, textColor_);
        wxTextAttr textStyle;
        textStyle.SetTextColour(textColor);
        textCtrl_->SetDefaultStyle(textStyle);
        textCtrl_->SetValue(v);
        if (rich_) {
            textCtrl_->SetStyle(0, textCtrl_->GetLastPosition(), textStyle);
        }
        if (wasReadOnly) textCtrl_->SetEditable(false);
        textCtrl_->SetInsertionPointEnd();
        textCtrl_->ShowPosition(textCtrl_->GetLastPosition());
        textCtrl_->Refresh();
    }
}

void RichEdit::AppendText(const wxString& t) {
    text_ += WxStringToUtf8(t);
    if (!textCtrl_) return;

    if (readOnly_) textCtrl_->SetEditable(true);
    textCtrl_->AppendText(t);
    if (readOnly_) textCtrl_->SetEditable(false);
    textCtrl_->SetInsertionPointEnd();
    textCtrl_->ShowPosition(textCtrl_->GetLastPosition());
    textCtrl_->Refresh();
}

void RichEdit::AppendText(const wxString& t, const wxColour& color) {
    text_ += WxStringToUtf8(t);
    if (!textCtrl_) return;

    const bool wasReadOnly = readOnly_;
    if (wasReadOnly) textCtrl_->SetEditable(true);

    const wxTextAttr previousStyle = textCtrl_->GetDefaultStyle();
    wxTextAttr style;
    style.SetTextColour(ResolveRichTextColor(manager_, color));
    textCtrl_->SetDefaultStyle(style);
    textCtrl_->AppendText(t);
    textCtrl_->SetDefaultStyle(previousStyle);

    if (wasReadOnly) textCtrl_->SetEditable(false);
    textCtrl_->SetInsertionPointEnd();
    textCtrl_->ShowPosition(textCtrl_->GetLastPosition());
    textCtrl_->Refresh();
}

void RichEdit::Clear() {
    if (textCtrl_) {
        const bool wasReadOnly = readOnly_;
        if (wasReadOnly) textCtrl_->SetEditable(true);
        textCtrl_->Clear();
        if (wasReadOnly) textCtrl_->SetEditable(false);
    }
    text_.clear();
}

void RichEdit::AppendBounded(std::string_view text, std::size_t maxCharacters) {
    auto value = Utf8ToUtf32(GetValueUtf8() + std::string(text));
    if (value.size() > maxCharacters) value.erase(0, value.size() - maxCharacters);
    SetValueUtf8(Utf32ToUtf8(value));
}

void RichEdit::SetReadOnly(bool r) {
    readOnly_ = r;
    if (textCtrl_) textCtrl_->SetEditable(!r);
}

void RichEdit::DoPaint(wxDC& dc, const wxRect& clipRect) {
    // Native control paints itself; draw background only if transparent=false
    if (!transparent_) Control::DoPaint(dc, clipRect);
}

} // namespace wxui
