#pragma once
/// libwxui — Label, Button, Option, Text, Progress, Slider, Edit controls.

#include "control.hpp"
#include <wx/font.h>
#include <wx/textctrl.h>

namespace wxui {

// ── Label ─────────────────────────────────────────────────────────────────
class Label : public Control {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "Label"; }
    /// Required height in logical pixels, including text padding. Wrapped
    /// text is cached until text, width or resolved font changes.
    int MeasureText(int width) const;

protected:
    wxFont ResolveFont(double size = 0, bool bold = false) const;
    /// Draw str at rc with given colour, respecting alignment/padding/ellipsis.
    void DrawTextContent(wxDC& dc, const wxRect& rc,
                         const std::string& str, const wxColour& color);

    // ── text attributes ─────────────────────────────────────────────────
    wxColour textColor_;
    wxColour disabledTextColor_;
    wxRect   textPadding_   {0, 0, 0, 0};
    int      fontId_        = -1;
    double   fontSize_      = 0;
    std::string fontFace_;
    bool     fontBold_      = false;
    mutable wxFont fontBase_;
    mutable std::map<std::pair<double,bool>,wxFont> fontCache_;
    int      hAlign_        = wxALIGN_LEFT;
    int      vAlign_        = wxALIGN_CENTER_VERTICAL;
    bool     endEllipsis_   = false;
    bool     showHtml_      = false;
    bool     wordWrap_      = false;
    const wxString& WrappedText(wxDC& dc, const std::string& text, int width) const;
    mutable std::string wrapText_;
    mutable wxString wrappedText_;
    mutable wxFont wrapFont_;
    mutable int wrapWidth_ = -1, wrapHeight_ = 0;

    // ── text effects (DUILib enabledeffect subset) ────────────────────────
    bool     enabledEffect_  = false;
    bool     enabledStroke_  = false;
    wxColour strokeColor_;
    uint8_t  transStroke_    = 255;
    bool     enabledShadow_  = false;
    int      gradientAngle_  = 0;
    int      gradientLength_ = 0;
    wxColour textColor1_;          ///< gradient second colour
    wxColour textShadowColorA_;
    wxColour textShadowColorB_;
    int      transShadow_    = 100;
    int      transText_      = 100;
    int      textShadowX_    = 0;
    int      textShadowY_    = 0;
    wxColour textShadowColor_;
};

// Exact text formatting: emphasize the two penultimate decimal digits and
// raise the final digit without converting the value to floating point.
class DecimalLabel : public Label {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    std::string GetTag() const override { return "DecimalLabel"; }
};

// ── Button ────────────────────────────────────────────────────────────────
enum class ButtonState { Normal, Hot, Pushed, Focused, Disabled };

class Button : public Label {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "Button"; }

    void OnMouseEnter(const wxPoint& pt) override;
    void OnMouseLeave()                  override;
    void OnButtonDown(const wxPoint& pt) override;
    void OnButtonUp(const wxPoint& pt)   override;
    void OnSetFocus()                    override;
    void OnKillFocus()                   override;

    [[nodiscard]] ButtonState GetState() const { return state_; }
    void SetState(ButtonState s) { state_ = s; Invalidate(); }

protected:
    ButtonState state_ = ButtonState::Normal;

    ImageSpec normalImage_;
    ImageSpec hotImage_;
    ImageSpec pushedImage_;
    ImageSpec focusedImage_;
    ImageSpec disabledImage_;

    wxColour  hotTextColor_;
    wxColour  pushedTextColor_;
    wxColour  focusedTextColor_;
};

// ── Option (radio / check button) ────────────────────────────────────────
class Option : public Button {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "Option"; }

    void OnButtonUp(const wxPoint& pt) override;

    void SetSelected(bool s);
    [[nodiscard]] bool              IsSelected() const { return selected_; }
    [[nodiscard]] const std::string& GetGroup()  const { return group_; }

private:
    enum class OptionStyle { Auto, Button, Radio, CheckBox };

    ImageSpec   selectedImage_;
    ImageSpec   foreImage_;
    wxColour    selectedTextColor_;
    wxColour    selectedBkColor_;
    std::string group_;
    bool        selected_ = false;
    bool        cancelSelected_ = false;
    OptionStyle optionStyle_ = OptionStyle::Auto;
};

// ── Text (rich-text with hyperlink support) ────────────────────────────────
class Text : public Label {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "Text"; }

    void OnButtonUp(const wxPoint& pt) override;
    void OnMouseMove(const wxPoint& pt) override;

private:
    /// Simple inline-link hit test; returns link href or "".
    std::string HitTestLink(const wxPoint& pt) const;
};

// ── Progress ──────────────────────────────────────────────────────────────
class Progress : public Label {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "Progress"; }

    void SetValue(int v);
    [[nodiscard]] int  GetValue()        const { return value_; }
    [[nodiscard]] int  GetMin()          const { return min_; }
    [[nodiscard]] int  GetMax()          const { return max_; }
    [[nodiscard]] bool IsHorizontal()    const { return hor_; }

protected:
    ImageSpec foreImage_;
    bool      hor_         = true;
    bool      stretchFore_ = true;
    int       min_         = 0;
    int       max_         = 100;
    int       value_       = 0;
};

// ── Slider ────────────────────────────────────────────────────────────────
class Slider : public Progress {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "Slider"; }

    void OnMouseMove(const wxPoint& pt)  override;
    void OnButtonDown(const wxPoint& pt) override;
    void OnButtonUp(const wxPoint& pt)   override;
    void OnMouseLeave()                  override;

private:
    wxRect  GetThumbRect() const;
    void    UpdateValueFromPoint(const wxPoint& pt);

    ImageSpec thumbImage_;
    ImageSpec thumbHotImage_;
    ImageSpec thumbPushedImage_;
    wxSize    thumbSize_  {10, 10};
    int       step_       = 1;
    bool      dragging_   = false;
    bool      thumbHot_   = false;
};

// ── Edit ──────────────────────────────────────────────────────────────────
/// Single-line text input. Owns a wxTextCtrl as a native child widget.
class Edit : public Label {
public:
    Edit();
    ~Edit() override;
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "Edit"; }

    void SetRect(const wxRect& rc) override;
    void OnManagerSet()            override;
    void OnSetFocus()              override;

    // ── value ─────────────────────────────────────────────────────────
    [[nodiscard]] wxString GetValue() const;
    [[nodiscard]] std::string GetValueUtf8() const { return WxStringToUtf8(GetValue()); }
    void SetValue(const wxString& v);
    void SetValue(std::string_view v) { SetValue(Utf8ToWxString(v)); }
    void SetValueUtf8(std::string_view v) { SetValue(Utf8ToWxString(v)); }
    void Clear() { SetValueUtf8({}); }
    void SetHint(std::string_view hint);
    void SetEnabled(bool e) override;
    void SetReadOnly(bool r);
    void SetPassword(bool p);
    void SetMaxChar(int n);

private:
    void CreateNativeCtrl();
    void SyncNativeCtrl();

    wxTextCtrl* textCtrl_   = nullptr;
    wxPoint nativeMargins_{-1, -1};

    bool      readOnly_     = false;
    bool      password_     = false;
    bool      multiline_    = false;
    int       maxChar_      = 255;
    std::string hint_;
    ImageSpec normalImage_;
    ImageSpec hotImage_;
    ImageSpec focusedImage_;
    ImageSpec disabledImage_;
    wxColour  nativeBkColor_;
};

} // namespace wxui
