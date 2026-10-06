#include <libwxui/text_editor.hpp>
#include <libwxui/appearance.hpp>
#include <wx/dcbuffer.h>
#include <algorithm>

namespace wxui {
class EditorScrollBar : public wxWindow {
public:
    EditorScrollBar(wxWindow* parent, bool horizontal, std::function<void(int)> scroll)
        : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
          horizontal_(horizontal), scroll_(std::move(scroll)) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(this);
            PaintScrollChrome(dc, GetClientRect(), Thumb(), dragging_ || hot_, GetParent()->GetBackgroundColour());
        });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
            const auto thumb = Thumb();
            const int coordinate = horizontal_ ? event.GetX() : event.GetY();
            if (thumb.Contains(event.GetPosition())) {
                dragging_ = true; offset_ = coordinate - (horizontal_ ? thumb.x : thumb.y);
                CaptureMouse();
            } else scroll_(std::clamp(position_ + (coordinate < (horizontal_ ? thumb.x : thumb.y) ? -page_ : page_), 0, Maximum()));
            Refresh();
        });
        Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
            hot_ = Thumb().Contains(event.GetPosition());
            if (dragging_) {
                const auto thumb = Thumb();
                const int travel = Length() - (horizontal_ ? thumb.width : thumb.height);
                const int coordinate = horizontal_ ? event.GetX() : event.GetY();
                if (travel > 0) scroll_(std::clamp(coordinate - offset_, 0, travel) * Maximum() / travel);
            }
            Refresh();
        });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
            dragging_ = false; if (HasCapture()) ReleaseMouse(); Refresh();
        });
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { dragging_ = false; Refresh(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hot_ = false; Refresh(); });
        Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& event) {
            const int steps = event.GetWheelRotation() / std::max(1, event.GetWheelDelta());
            scroll_(std::clamp(position_ - steps * (horizontal_ ? 40 : 3), 0, Maximum()));
        });
    }
    void SetMetrics(int total, int page, int position) {
        total = std::max(1, total); page = std::max(1, page);
        position = std::clamp(position, 0, std::max(0, total - page));
        if (total_ == total && page_ == page && position_ == position) return;
        total_ = total; page_ = page; position_ = position;
        Refresh(false);
    }
private:
    int Maximum() const { return std::max(0, total_ - page_); }
    int Length() const { return horizontal_ ? GetClientSize().x : GetClientSize().y; }
    wxRect Thumb() const {
        const int length = std::max(0, Length());
        const int size = std::clamp(length * page_ / total_, std::min(24, length), length);
        const int offset = Maximum() ? position_ * (length - size) / Maximum() : 0;
        const int thickness = horizontal_ ? GetClientSize().y : GetClientSize().x;
        return horizontal_ ? wxRect(offset, 3, size, std::max(0, thickness - 6))
                           : wxRect(3, offset, std::max(0, thickness - 6), size);
    }
    bool horizontal_, dragging_ = false, hot_ = false;
    int total_ = 1, page_ = 1, position_ = 0, offset_ = 0;
    std::function<void(int)> scroll_;
};

TextEditor::TextEditor(wxWindow* parent, wxWindowID id, const wxString& value,
                       const wxPoint& position, const wxSize& size, long style)
    : wxPanel(parent, id, position, size, wxBORDER_NONE | wxCLIP_CHILDREN) {
    // Keep native single-line/password behavior for legacy RichEdit XML too.
    if ((style & wxTE_PASSWORD) || !(style & wxTE_MULTILINE)) {
        const long nativeStyle = (style & ~wxTE_MULTILINE) | wxBORDER_NONE;
        plain_ = new wxTextCtrl(this, wxID_ANY, value, {}, size, nativeStyle);
        SetFont(InterfaceFont());
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { plain_->SetSize(GetClientSize()); event.Skip(); });
        return;
    }
    editor_ = new wxStyledTextCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    editor_->SetCodePage(wxSTC_CP_UTF8);
    editor_->SetUseVerticalScrollBar(false);
    editor_->SetUseHorizontalScrollBar(false);
    editor_->SetScrollWidth(1);
    editor_->SetScrollWidthTracking(true);
    editor_->SetEndAtLastLine(true);
    editor_->SetMarginCount(0);
    editor_->SetMarginLeft(8); editor_->SetMarginRight(8);
    editor_->SetTabWidth(2);
    editor_->SetUseTabs(false);
    editor_->SetFontQuality(wxSTC_EFF_QUALITY_LCD_OPTIMIZED);
    vertical_ = new EditorScrollBar(this, false, [this](int position) { editor_->SetFirstVisibleLine(position); UpdateScrollbars(); });
    horizontal_ = new EditorScrollBar(this, true, [this](int position) { editor_->SetXOffset(position); UpdateScrollbars(); });
    SetWrap((style & wxTE_DONTWRAP) == 0 && (style & wxHSCROLL) == 0);
    SetFont(InterfaceFont());
    SetValue(value);
    SetEditable((style & wxTE_READONLY) == 0);
    editor_->Bind(wxEVT_STC_CHANGE, [this](wxStyledTextEvent&) {
        if (setting_) return;
        UpdateScrollbars();
        wxCommandEvent changed(wxEVT_TEXT, GetId()); changed.SetEventObject(this);
        GetEventHandler()->ProcessEvent(changed);
    });
    editor_->Bind(wxEVT_STC_UPDATEUI, [this](wxStyledTextEvent&) { UpdateScrollbars(); });
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { UpdateScrollbars(); event.Skip(); });
    // Keep Tab for request indentation, while read-only/ordinary text views
    // participate in native focus traversal.
    if (!(style & wxTE_PROCESS_TAB)) editor_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_TAB) Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
        else event.Skip();
    });
}
wxString TextEditor::GetValue() const { return plain_ ? plain_->GetValue() : editor_->GetText(); }
int TextEditor::BytePosition(long position) const {
    if (position < 0) return editor_->GetTextLength();
    return static_cast<int>(GetValue().Left(position).ToUTF8().length());
}
long TextEditor::GetLastPosition() const { return static_cast<long>(GetValue().length()); }
void TextEditor::SetValue(const wxString& text) {
    if (plain_) { plain_->SetValue(text); return; }
    const bool readonly = editor_->GetReadOnly();
    setting_ = true;
    editor_->SetReadOnly(false); editor_->SetText(text); editor_->SetReadOnly(readonly);
    editor_->EmptyUndoBuffer(); editor_->SetScrollWidth(1);
    setting_ = false;
    UpdateScrollbars();
    wxCommandEvent changed(wxEVT_TEXT, GetId()); changed.SetEventObject(this);
    GetEventHandler()->ProcessEvent(changed);
}
void TextEditor::AppendText(const wxString& text) {
    if (plain_) { plain_->AppendText(text); return; }
    const auto start = GetLastPosition();
    editor_->AppendText(text);
    if (defaultStyle_.HasTextColour()) SetStyle(start, GetLastPosition(), defaultStyle_);
    UpdateScrollbars();
}
void TextEditor::SetEditable(bool editable) { if (plain_) plain_->SetEditable(editable); else editor_->SetReadOnly(!editable); }
bool TextEditor::IsEditable() const { return plain_ ? plain_->IsEditable() : !editor_->GetReadOnly(); }
void TextEditor::SetInsertionPoint(long position) { if (plain_) plain_->SetInsertionPoint(position); else editor_->GotoPos(BytePosition(position)); }
void TextEditor::SetInsertionPointEnd() { if (plain_) plain_->SetInsertionPointEnd(); else editor_->GotoPos(editor_->GetTextLength()); }
void TextEditor::ShowPosition(long position) { if (plain_) plain_->ShowPosition(position); else { editor_->GotoPos(BytePosition(position)); UpdateScrollbars(); } }
void TextEditor::SetSelection(long from, long to) { if (plain_) { plain_->SetSelection(from,to); return; } if (from == -1 && to == -1) editor_->SelectAll(); else editor_->SetSelection(BytePosition(from), BytePosition(to)); }
void TextEditor::Copy() { if (plain_) plain_->Copy(); else editor_->Copy(); }
void TextEditor::Paste() { if (plain_) plain_->Paste(); else editor_->Paste(); }
void TextEditor::SetFocus() { if (plain_) plain_->SetFocus(); else editor_->SetFocus(); }
bool TextEditor::SetFont(const wxFont& font) {
    const bool result = wxPanel::SetFont(font);
    ApplyAppearance(); return result;
}
void TextEditor::SetOwnBackgroundColour(const wxColour& color) {
    background_ = color; wxPanel::SetOwnBackgroundColour(color); ApplyAppearance();
}
void TextEditor::SetOwnForegroundColour(const wxColour& color) { foreground_ = color; ApplyAppearance(); }
void TextEditor::SetDefaultStyle(const wxTextAttr& style) { defaultStyle_ = style; if (plain_) plain_->SetDefaultStyle(style); }
void TextEditor::SetStyle(long from, long to, const wxTextAttr& style) {
    if (plain_) { plain_->SetStyle(from, to, style); return; }
    if (json_ || !style.HasTextColour()) return;
    auto [it, inserted] = colorStyles_.try_emplace(style.GetTextColour().GetRGB(), 64 + static_cast<int>(colorStyles_.size()));
    if (it->second > 255) return;
    if (inserted) {
        editor_->StyleSetFont(it->second, GetFont());
        editor_->StyleSetForeground(it->second, style.GetTextColour());
        editor_->StyleSetBackground(it->second, background_);
    }
    const int start = BytePosition(from), end = BytePosition(to);
    editor_->StartStyling(start); editor_->SetStyling(std::max(0, end - start), it->second);
}
void TextEditor::SetJsonHighlight(bool enabled) { json_ = enabled; ApplyAppearance(); }
void TextEditor::SetWrap(bool wrap) { wrap_ = wrap; if (plain_) return; editor_->SetWrapMode(wrap ? wxSTC_WRAP_WORD : wxSTC_WRAP_NONE); if (wrap) editor_->SetXOffset(0); }
void TextEditor::ApplyAppearance() {
    if (plain_) {
        plain_->SetFont(GetFont()); plain_->SetOwnBackgroundColour(background_);
        plain_->SetOwnForegroundColour(foreground_); return;
    }
    editor_->StyleSetFont(wxSTC_STYLE_DEFAULT, GetFont());
    editor_->StyleSetForeground(wxSTC_STYLE_DEFAULT, foreground_);
    editor_->StyleSetBackground(wxSTC_STYLE_DEFAULT, background_);
    editor_->StyleClearAll(); colorStyles_.clear();
    editor_->SetCaretForeground(foreground_);
    editor_->SetSelBackground(true, ChromeFor(background_).divider);
    editor_->SetLexer(json_ ? wxSTC_LEX_JSON : wxSTC_LEX_NULL);
    if (json_) {
        const bool dark = background_.GetLuminance() < 0.5;
        editor_->StyleSetForeground(wxSTC_JSON_PROPERTYNAME, dark ? wxColour(156,220,254) : wxColour(31,86,140));
        editor_->StyleSetForeground(wxSTC_JSON_STRING, dark ? wxColour(206,145,120) : wxColour(148,67,45));
        editor_->StyleSetForeground(wxSTC_JSON_NUMBER, dark ? wxColour(181,206,168) : wxColour(42,115,70));
        editor_->StyleSetForeground(wxSTC_JSON_KEYWORD, dark ? wxColour(86,156,214) : wxColour(114,66,160));
        editor_->SetKeyWords(0, "true false null");
        editor_->Colourise(0, -1);
    }
    UpdateScrollbars();
}
void TextEditor::UpdateScrollbars() {
    if (updating_ || !vertical_ || !horizontal_) return;
    updating_ = true;
    constexpr int thickness = 12;
    const auto size = GetClientSize();
    bool vertical = vertical_->IsShown(), horizontal = horizontal_->IsShown();
    const auto resize = [](wxWindow* window, const wxRect& rect) {
        if (window->GetRect() != rect) window->SetSize(rect);
    };
    // Start from the current viewport. Resetting to a full-size viewport on
    // every caret/scroll notification repeatedly resized and repainted STC.
    for (int pass = 0; pass < 3; ++pass) {
        resize(editor_, {0, 0, std::max(0, size.x - (vertical ? thickness : 0)), std::max(0, size.y - (horizontal ? thickness : 0))});
        const int last = std::max(0, editor_->GetLineCount() - 1);
        const int lines = editor_->VisibleFromDocLine(last) + editor_->WrapCount(last);
        const bool nextVertical = lines > std::max(1, editor_->LinesOnScreen());
        const bool nextHorizontal = !wrap_ && editor_->GetScrollWidth() >
            std::max(0, size.x - (nextVertical ? thickness : 0));
        if (vertical == nextVertical && horizontal == nextHorizontal) break;
        vertical = nextVertical; horizontal = nextHorizontal;
    }
    const int width = std::max(0, size.x - (vertical ? thickness : 0));
    const int height = std::max(0, size.y - (horizontal ? thickness : 0));
    resize(editor_, {0, 0, width, height});
    resize(vertical_, {width, 0, thickness, height});
    resize(horizontal_, {0, height, width, thickness});
    vertical_->Show(vertical); horizontal_->Show(horizontal);
    const int last = std::max(0, editor_->GetLineCount() - 1);
    vertical_->SetMetrics(editor_->VisibleFromDocLine(last) + editor_->WrapCount(last), editor_->LinesOnScreen(), editor_->GetFirstVisibleLine());
    horizontal_->SetMetrics(editor_->GetScrollWidth(), width, editor_->GetXOffset());
    updating_ = false;
}
} // namespace wxui
