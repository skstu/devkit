#include <libwxui.hpp>
#include <libwxui/appearance.hpp>

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/textctrl.h>
#include <wx/log.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>

namespace wxui {

namespace {

wxColour ResolveControlTextColor(UIManager* manager,
                                 const wxColour& requested,
                                 bool enabled) {
    if (requested.IsOk()) return requested;
    if (manager && manager->GetRoot()) {
        return enabled ? manager->GetRoot()->GetDefaultFontColor()
                       : manager->GetRoot()->GetDisabledFontColor();
    }
    return *wxBLACK;
}

wxColour BlendColour(const wxColour& from, const wxColour& to, double amount) {
    if (!from.IsOk()) return from;
    amount = std::clamp(amount, 0.0, 1.0);
    const auto mix = [amount](unsigned char a, unsigned char b) {
        return static_cast<unsigned char>(a + (b - a) * amount);
    };
    return wxColour(mix(from.Red(), to.Red()),
                    mix(from.Green(), to.Green()),
                    mix(from.Blue(), to.Blue()),
                    from.Alpha());
}

wxColour ResolveControlBkColor(const Control* control) {
    if (!control) return *wxWHITE;
    if (control->GetBkColor().IsOk() && control->GetBkColor().Alpha() != 0) {
        return control->GetBkColor();
    }
    for (const Container* parent = control->GetParent();
         parent != nullptr;
         parent = parent->GetParent()) {
        const wxColour colour = parent->GetBkColor();
        if (colour.IsOk() && colour.Alpha() != 0) return colour;
    }
    if (const UIManager* manager = control->GetManager()) {
        if (const Window* root = manager->GetRoot()) {
            const wxColour colour = root->GetBkColor();
            if (colour.IsOk() && colour.Alpha() != 0) return colour;
        }
    }
    return *wxWHITE;
}

void DisableTextCtrlSmartSubstitutions(wxTextCtrl* textCtrl) {
#if defined(__WXOSX__)
    if (textCtrl) textCtrl->OSXDisableAllSmartSubstitutions();
#else
    (void)textCtrl;
#endif
}

void BindTextCtrlEditShortcuts(wxTextCtrl* textCtrl) {
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

// ══════════════════════════════════════════════════════════════════════════
// Label
// ══════════════════════════════════════════════════════════════════════════

void Label::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "fontsize") { double size = 0; if (Utf8ToWxString(val).ToDouble(&size)) fontSize_ = std::clamp(size, 1.0, 256.0); fontCache_.clear(); return; }
    if (key == "fontface") { fontFace_ = val; fontCache_.clear(); return; }
    if (key == "bold") { fontBold_ = ParseBOOL(val); fontCache_.clear(); return; }
    if (key == "textcolor"  || key == "textColor")   { const auto next=ParseDWORD(val); if(textColor_!=next){textColor_=next;Invalidate();} return; }
    if (key == "disabledtextcolor" || key == "disabledTextColor")
                                                     { disabledTextColor_ = ParseDWORD(val); return; }
    if (key == "textpadding" || key == "textPadding"){ textPadding_       = ParseRECT(val);  return; }
    if (key == "font"  || key == "fontid")           { fontId_            = ParseINT(val);   return; }
    if (key == "align")                              {
        if (val == "center") { hAlign_ = wxALIGN_CENTER_HORIZONTAL; }
        else if (val == "right") { hAlign_ = wxALIGN_RIGHT; }
        else { hAlign_ = wxALIGN_LEFT; }
        return;
    }
    if (key == "valign") {
        if (val == "center") { vAlign_ = wxALIGN_CENTER_VERTICAL; }
        else if (val == "bottom") { vAlign_ = wxALIGN_BOTTOM; }
        else { vAlign_ = wxALIGN_TOP; }
        return;
    }
    if (key == "endellipsis" || key == "endEllipsis") { endEllipsis_ = ParseBOOL(val); return; }
    if (key == "showhtml"    || key == "showHtml")    { showHtml_    = ParseBOOL(val); return; }
    if (key == "wordwrap") { const bool next = ParseBOOL(val); if (wordWrap_ != next) { wordWrap_ = next; Invalidate(); } return; }
    if (key == "enabledeffect")  { enabledEffect_ = ParseBOOL(val); return; }
    if (key == "enabledstroke")  { enabledStroke_ = ParseBOOL(val); return; }
    if (key == "strokecolor")    { strokeColor_   = ParseDWORD(val);return; }
    if (key == "enabledshadow")  { enabledShadow_ = ParseBOOL(val); return; }
    if (key == "gradientangle")  { gradientAngle_ = ParseINT(val);  return; }
    if (key == "gradientlength") { gradientLength_= ParseINT(val);  return; }
    if (key == "textcolor1")     { textColor1_    = ParseDWORD(val);return; }
    if (key == "textshadowx")    { textShadowX_   = ParseINT(val);  return; }
    if (key == "textshadowy")    { textShadowY_   = ParseINT(val);  return; }
    if (key == "textshadowcolor")
                                 { textShadowColor_= ParseDWORD(val);return; }
    Control::SetAttribute(key, val);
}

void Label::DrawTextContent(wxDC& dc, const wxRect& textRect,
                             const std::string& str, const wxColour& color) {
    if (str.empty()) return;
    dc.SetTextForeground(color.IsOk() ? color : *wxBLACK);
    dc.SetBackgroundMode(wxTRANSPARENT);
    dc.SetFont(ResolveFont());

    int flags = 0;
    flags |= hAlign_;
    flags |= vAlign_;

    wxRect r{
        textRect.x + textPadding_.x,
        textRect.y + textPadding_.y,
        std::max(0, textRect.width - textPadding_.x - textPadding_.width),
        std::max(0, textRect.height - textPadding_.y - textPadding_.height)
    };
    if (r.IsEmpty()) return;
    auto shown = wordWrap_ ? WrappedText(dc, str, r.width) : Utf8ToWxString(str);
    if (endEllipsis_ && !wordWrap_) shown = wxControl::Ellipsize(shown, dc, wxELLIPSIZE_END, r.width);
    dc.DrawLabel(shown, r, flags);
}

const wxString& Label::WrappedText(wxDC& dc, const std::string& text, int width) const {
    width = std::max(1, width);
    const auto font = dc.GetFont();
    if (wrapWidth_ == width && wrapText_ == text && wrapFont_ == font) return wrappedText_;
    wrapText_ = text;
    wrapWidth_ = width;
    wrapFont_ = font;
    wrappedText_.clear();
    const auto input = Utf8ToWxString(text);
    size_t paragraphStart = 0;
    while (paragraphStart <= input.length()) {
        const auto end = input.find('\n', paragraphStart);
        const auto paragraph = input.substr(paragraphStart, end == wxString::npos ? wxString::npos : end - paragraphStart);
        wxArrayInt extents;
        if (!dc.GetPartialTextExtents(paragraph, extents) || extents.size() != paragraph.length()) {
            extents.clear();
            for (size_t i = 1; i <= paragraph.length(); ++i) extents.push_back(dc.GetTextExtent(paragraph.substr(0, i)).x);
        }
        size_t start = 0;
        while (start < paragraph.length()) {
            const int offset = start ? extents[start - 1] : 0;
            const auto first = extents.begin() + start;
            const auto last = std::upper_bound(first, extents.end(), offset + width);
            size_t next = std::max(start + 1, size_t(last - extents.begin()));
            // Prefer a word boundary for Latin text; CJK may break at a character.
            if (next < paragraph.length()) {
                for (size_t i = next; i > start; --i) {
                    if (paragraph[i - 1] == ' ' || paragraph[i - 1] == '\t') { next = i; break; }
                }
            }
            wrappedText_ += paragraph.substr(start, next - start);
            start = next;
            if (start < paragraph.length()) {
                wrappedText_ += '\n';
                while (start < paragraph.length() && (paragraph[start] == ' ' || paragraph[start] == '\t')) ++start;
            }
        }
        if (end == wxString::npos) break;
        wrappedText_ += '\n';
        paragraphStart = end + 1;
    }
    wxCoord height = 0;
    dc.GetMultiLineTextExtent(wrappedText_, nullptr, &height);
    wrapHeight_ = height;
    return wrappedText_;
}

int Label::MeasureText(int width) const {
    const auto measure = [&](wxDC& dc) {
        dc.SetFont(ResolveFont());
        wxCoord height = 0;
        if (wordWrap_) {
            WrappedText(dc, text_, width - textPadding_.x - textPadding_.width);
            height = wrapHeight_;
        } else {
            dc.GetMultiLineTextExtent(Utf8ToWxString(text_), nullptr, &height);
        }
        return height + textPadding_.y + textPadding_.height;
    };
    if (manager_) { wxClientDC dc(manager_); return measure(dc); }
    wxBitmap bitmap(1, 1);
    wxMemoryDC dc(bitmap);
    return measure(dc);
}

wxFont Label::ResolveFont(double size, bool bold) const {
    auto font = manager_ ? manager_->GetUIFont() : InterfaceFont();
    const double pixels = size > 0 ? size : fontSize_;
    if (pixels <= 0 && !bold && !fontBold_ && fontFace_.empty()) return font;
    if (!fontBase_.IsOk() || fontBase_ != font) { fontCache_.clear(); fontBase_ = font; }
    const auto key = std::pair{pixels,bold || fontBold_};
    if (const auto found = fontCache_.find(key); found != fontCache_.end()) return found->second;
    if (!fontFace_.empty()) font.SetFaceName(Utf8ToWxString(fontFace_));
    if (pixels > 0) font = FontWithLogicalSize(font, pixels);
    if (bold || fontBold_) font.SetWeight(wxFONTWEIGHT_BOLD);
    if (fontCache_.size() == 8) fontCache_.clear();
    fontCache_.emplace(key,font);
    return font;
}

void DecimalLabel::DoPaint(wxDC& dc, const wxRect&) {
    DrawBkColor(dc);
    const auto color = ResolveControlTextColor(manager_, textColor_, enabled_);
    dc.SetTextForeground(color);
    dc.SetBackgroundMode(wxTRANSPARENT);
    if (text_ == "—" || text_.find('.') == std::string::npos || text_.size() < 3) {
        dc.SetFont(ResolveFont(23.25, true));
        dc.DrawLabel(Utf8ToWxString(text_), rect_, wxALIGN_CENTER);
    } else {
        const auto lead = Utf8ToWxString(text_.substr(0, text_.size() - 3));
        const auto middle = Utf8ToWxString(text_.substr(text_.size() - 3, 2));
        const auto tail = Utf8ToWxString(text_.substr(text_.size() - 1));
        dc.SetFont(ResolveFont(15));
        const auto a = dc.GetTextExtent(lead);
        dc.SetFont(ResolveFont(23.25, true));
        const auto b = dc.GetTextExtent(middle);
        dc.SetFont(ResolveFont(12.5));
        const auto c = dc.GetTextExtent(tail);
        const int x = rect_.x + (rect_.width - a.x - b.x - c.x) / 2;
        const int baseline = rect_.y + (rect_.height + b.y) / 2;
        dc.SetFont(ResolveFont(15));
        dc.DrawText(lead, x, baseline - a.y);
        dc.SetFont(ResolveFont(23.25, true));
        dc.DrawText(middle, x + a.x, baseline - b.y);
        dc.SetFont(ResolveFont(12.5));
        dc.DrawText(tail, x + a.x + b.x, baseline - b.y + 2);
    }
    DrawBorder(dc);
}

void Label::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Control::DoPaint(dc, clipRect);
    const wxColour col = ResolveControlTextColor(
        manager_, enabled_ ? textColor_ : disabledTextColor_, enabled_);
    DrawTextContent(dc, rect_, text_, col);
}

// ══════════════════════════════════════════════════════════════════════════
// Button
// ══════════════════════════════════════════════════════════════════════════

void Button::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "normalimage" || key == "normalImage")   { normalImage_  = ParseImageSpec(val); return; }
    if (key == "hotimage"    || key == "hotImage")      { hotImage_     = ParseImageSpec(val); return; }
    if (key == "pushedimage" || key == "pushedImage")   { pushedImage_  = ParseImageSpec(val); return; }
    if (key == "focusedimage"|| key == "focusedImage")  { focusedImage_ = ParseImageSpec(val); return; }
    if (key == "disabledimage"||key=="disabledImage")   { disabledImage_= ParseImageSpec(val); return; }
    if (key == "hottextcolor"|| key == "hotTextColor")  { hotTextColor_  = ParseDWORD(val);    return; }
    if (key == "pushedtextcolor"||key=="pushedTextColor"){ pushedTextColor_=ParseDWORD(val);   return; }
    if (key == "focusedtextcolor"||key=="focusedTextColor"){ focusedTextColor_=ParseDWORD(val);return; }
    Label::SetAttribute(key, val);
}

void Button::DoPaint(wxDC& dc, const wxRect& clipRect) {
    (void)clipRect;

    // Background
    const wxColour originalBk = bkColor_, originalBk2 = bkColor2_;
    if (bkColor_.IsOk() && bkColor_.Alpha() != 0) {
        if (!enabled_) {
            bkColor_ = BlendColour(bkColor_, *wxWHITE, 0.45);
        } else if (state_ == ButtonState::Hot) {
            bkColor_ = BlendColour(bkColor_, *wxWHITE, 0.14);
        } else if (state_ == ButtonState::Pushed) {
            bkColor_ = BlendColour(bkColor_, *wxBLACK, 0.10);
        } else if (state_ == ButtonState::Focused) {
            bkColor_ = BlendColour(bkColor_, *wxWHITE, 0.08);
        }
    }
    if (bkColor2_.IsOk()) {
        if (!enabled_) bkColor2_ = BlendColour(bkColor2_, *wxWHITE, 0.45);
        else if (state_ == ButtonState::Hot) bkColor2_ = BlendColour(bkColor2_, *wxWHITE, 0.14);
        else if (state_ == ButtonState::Pushed) bkColor2_ = BlendColour(bkColor2_, *wxBLACK, 0.10);
    }
    DrawBkColor(dc);
    bkColor2_ = originalBk2;
    bkColor_ = originalBk;
    DrawBkImage(dc);

    // Pick state image
    const ImageSpec* img = &normalImage_;
    wxColour        txtColor = ResolveControlTextColor(manager_, textColor_, true);

    if (!enabled_) {
        img      = disabledImage_.IsEmpty() ? &normalImage_ : &disabledImage_;
        txtColor = ResolveControlTextColor(manager_, disabledTextColor_, false);
    } else {
        switch (state_) {
        case ButtonState::Hot:
            img      = hotImage_.IsEmpty()     ? &normalImage_ : &hotImage_;
            if (hotTextColor_.IsOk()) txtColor = hotTextColor_;
            break;
        case ButtonState::Pushed:
            img      = pushedImage_.IsEmpty()  ? &normalImage_ : &pushedImage_;
            if (pushedTextColor_.IsOk()) txtColor = pushedTextColor_;
            break;
        case ButtonState::Focused:
            img      = focusedImage_.IsEmpty() ? &normalImage_ : &focusedImage_;
            if (focusedTextColor_.IsOk()) txtColor = focusedTextColor_;
            break;
        default: break;
        }
    }

    if (img && !img->IsEmpty() && manager_) {
        wxBitmap bmp = manager_->LoadBitmap(img->path);
        if (bmp.IsOk())
            dc.DrawBitmap(bmp, rect_.GetTopLeft());
    }

    DrawTextContent(dc, rect_, text_, txtColor);
    DrawBorder(dc);
}

void Button::OnMouseEnter(const wxPoint&) {
    if (!enabled_) return;
    state_ = ButtonState::Hot;
    Invalidate();
}

void Button::OnMouseLeave() {
    if (!enabled_) return;
    state_ = ButtonState::Normal;
    Invalidate();
}

void Button::OnButtonDown(const wxPoint&) {
    if (!enabled_) return;
    state_ = ButtonState::Pushed;
    Invalidate();
}

void Button::OnButtonUp(const wxPoint& pt) {
    if (!enabled_) return;
    state_ = rect_.Contains(pt) ? ButtonState::Hot : ButtonState::Normal;
    Invalidate();
    if (rect_.Contains(pt)) FireNotify("click");
}

// ══════════════════════════════════════════════════════════════════════════
// Option
// ══════════════════════════════════════════════════════════════════════════

void Option::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "selected")    { SetSelected(ParseBOOL(val)); return; }
    if (key == "group")       { group_ = val;                return; }
    if (key == "optionstyle" || key == "optionStyle" ||
        key == "style" || key == "display") {
        const std::string normalized = NormalizeXmlIdentifier(val);
        if (normalized == "radio") optionStyle_ = OptionStyle::Radio;
        else if (normalized == "checkbox" || normalized == "check")
            optionStyle_ = OptionStyle::CheckBox;
        else if (normalized == "button") optionStyle_ = OptionStyle::Button;
        else optionStyle_ = OptionStyle::Auto;
        return;
    }
    if (key == "cancelselected" || key == "cancelSelected" ||
        key == "cancelable" || key == "allowunselect" ||
        key == "allowUnselect") {
        cancelSelected_ = ParseBOOL(val);
        return;
    }
    if (key == "selectedimage"||key=="selectedImage") { selectedImage_=ParseImageSpec(val); return; }
    if (key == "foreimage"   ||key=="foreImage")      { foreImage_=ParseImageSpec(val);     return; }
    if (key == "selectedtextcolor"||key=="selectedTextColor")
                              { selectedTextColor_ = ParseDWORD(val); return; }
    if (key == "selectedbkcolor"||key=="selectedBkColor")
                              { selectedBkColor_ = ParseDWORD(val); return; }
    Button::SetAttribute(key, val);
}

void Option::DoPaint(wxDC& dc, const wxRect& clipRect) {
    if (optionStyle_ == OptionStyle::CheckBox) {
        const int indicator = std::min(18, std::max(14, rect_.height - 12));
        const int gap = 8;
        const int x = rect_.x + 2;
        const int y = rect_.y + (rect_.height - indicator) / 2;
        const wxRect indicatorRect(x, y, indicator, indicator);
        const wxColour border = selected_
            ? (selectedBkColor_.IsOk() ? selectedBkColor_
                                       : wxColour(0x25, 0x63, 0xEB))
            : (borderColor_.IsOk() ? borderColor_ : wxColour(0x94, 0xA3, 0xB8));
        const wxColour fill = selected_
            ? (selectedBkColor_.IsOk() ? selectedBkColor_
                                       : wxColour(0x25, 0x63, 0xEB))
            : ResolveControlBkColor(this);
        const wxColour text = selected_ && selectedTextColor_.IsOk()
            ? selectedTextColor_
            : ResolveControlTextColor(manager_, textColor_, enabled_);

        constexpr int indicatorScale = 4;
        wxBitmap indicatorBitmap(indicator * indicatorScale,
                                 indicator * indicatorScale, 32);
        wxMemoryDC memDc(indicatorBitmap);
        memDc.SetBackground(wxBrush(ResolveControlBkColor(this)));
        memDc.Clear();

        bool drewSmoothIndicator = false;
        if (wxGraphicsContext* gc = wxGraphicsContext::Create(memDc)) {
            constexpr double scale = static_cast<double>(indicatorScale);
            gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
            gc->SetPen(wxPen(border,
                             selected_ ? 2 * indicatorScale : indicatorScale));
            gc->SetBrush(wxBrush(fill));
            gc->DrawRoundedRectangle(scale, scale,
                                     indicator * scale - 2.0 * scale,
                                     indicator * scale - 2.0 * scale,
                                     3.0 * scale);
            if (selected_) {
                gc->SetPen(wxPen(text, 2 * indicatorScale));
                wxGraphicsPath tick = gc->CreatePath();
                tick.MoveToPoint(indicator * scale * 0.25,
                                 indicator * scale * 0.53);
                tick.AddLineToPoint(indicator * scale * 0.43,
                                    indicator * scale * 0.70);
                tick.AddLineToPoint(indicator * scale * 0.76,
                                    indicator * scale * 0.32);
                gc->StrokePath(tick);
            }
            delete gc;
            memDc.SelectObject(wxNullBitmap);
            wxImage image = indicatorBitmap.ConvertToImage();
            if (image.IsOk()) {
                image.Rescale(indicator, indicator, wxIMAGE_QUALITY_HIGH);
                dc.DrawBitmap(wxBitmap(image), indicatorRect.GetTopLeft(),
                              false);
                drewSmoothIndicator = true;
            }
        } else {
            memDc.SelectObject(wxNullBitmap);
        }

        if (!drewSmoothIndicator) {
            dc.SetPen(wxPen(border, selected_ ? 2 : 1));
            dc.SetBrush(wxBrush(fill));
            dc.DrawRoundedRectangle(indicatorRect, 3);
        }
        if (selected_) {
            dc.SetPen(wxPen(text, 2));
            const int left = indicatorRect.x + indicator / 4;
            const int midX = indicatorRect.x + indicator / 2;
            const int right = indicatorRect.x + indicator - indicator / 5;
            const int midY = indicatorRect.y + indicator - indicator / 3;
            const int topY = indicatorRect.y + indicator / 4;
            if (!drewSmoothIndicator) {
                dc.DrawLine(left, indicatorRect.y + indicator / 2, midX, midY);
                dc.DrawLine(midX, midY, right, topY);
            }
        }

        wxRect textRect(
            indicatorRect.GetRight() + 1 + gap, rect_.y,
            std::max(0, rect_.GetRight() - indicatorRect.GetRight() - gap),
            rect_.height);
        const int leftPadding = std::max(0, textPadding_.x);
        const int rightPadding = std::max(0, textPadding_.width);
        textRect.x += leftPadding;
        textRect.width = std::max(0, textRect.width - leftPadding - rightPadding);

        dc.SetTextForeground(text);
        dc.SetBackgroundMode(wxTRANSPARENT);
        if (manager_) dc.SetFont(manager_->GetUIFont());
        dc.DrawLabel(Utf8ToWxString(text_), textRect,
                     wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
        return;
    }

    const bool radioStyle =
        optionStyle_ == OptionStyle::Radio ||
        (optionStyle_ == OptionStyle::Auto && group_.empty() &&
         selectedImage_.IsEmpty() && foreImage_.IsEmpty());
    if (radioStyle) {
        const int indicator = std::min(16, std::max(12, rect_.height - 10));
        const int gap = 8;
        const int x = rect_.x;
        const int y = rect_.y + (rect_.height - indicator) / 2;
        const wxRect indicatorRect(x, y, indicator, indicator);

        const wxColour border = selected_
            ? (selectedTextColor_.IsOk() ? selectedTextColor_
                                         : wxColour(0x25, 0x63, 0xEB))
            : (borderColor_.IsOk() ? borderColor_ : wxColour(0x94, 0xA3, 0xB8));
        const wxColour text = selected_ && selectedTextColor_.IsOk()
            ? selectedTextColor_
            : ResolveControlTextColor(manager_, textColor_, enabled_);
        const wxColour background = ResolveControlBkColor(this);

        const int scale = 4;
        wxBitmap indicatorBitmap(indicator * scale, indicator * scale, 32);
        wxMemoryDC memDc(indicatorBitmap);
        memDc.SetBackground(wxBrush(background));
        memDc.Clear();

        bool drewSmoothIndicator = false;
        if (wxGraphicsContext* gc = wxGraphicsContext::Create(memDc)) {
            gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

            const double outerInset = 1.0 * scale;
            const double bitmapSize = static_cast<double>(indicator * scale);
            const double outerSize = std::max(0.0, bitmapSize - outerInset * 2.0);
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(border));
            gc->DrawEllipse(outerInset, outerInset, outerSize, outerSize);

            const double ringWidth = (selected_ ? 3.0 : 2.0) * scale;
            const double innerInset = outerInset + ringWidth;
            const double innerSize = std::max(0.0, bitmapSize - innerInset * 2.0);
            gc->SetBrush(wxBrush(background));
            gc->DrawEllipse(innerInset, innerInset, innerSize, innerSize);

            if (selected_) {
                const double dotSize = std::max(4.0, indicator * 0.36) * scale;
                const double dotOffset = (bitmapSize - dotSize) / 2.0;
                gc->SetBrush(wxBrush(border));
                gc->DrawEllipse(dotOffset, dotOffset, dotSize, dotSize);
            }
            delete gc;
            memDc.SelectObject(wxNullBitmap);

            wxImage image = indicatorBitmap.ConvertToImage();
            if (image.IsOk()) {
                image.Rescale(indicator, indicator, wxIMAGE_QUALITY_HIGH);
                dc.DrawBitmap(wxBitmap(image), indicatorRect.GetTopLeft(), false);
                drewSmoothIndicator = true;
            }
        } else {
            memDc.SelectObject(wxNullBitmap);
        }

        if (!drewSmoothIndicator) {
            dc.SetPen(wxPen(border, selected_ ? 2 : 1));
            dc.SetBrush(wxBrush(background));
            dc.DrawEllipse(indicatorRect);

            if (selected_) {
                const int inset = std::max(4, indicator / 3);
                wxRect fill = indicatorRect;
                fill.Deflate(inset);
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(border));
                dc.DrawEllipse(fill);
            }
        }

        wxRect textRect(rect_.x + indicator + gap, rect_.y,
                        std::max(0, rect_.width - indicator - gap),
                        rect_.height);
        const int leftPadding = std::max(0, textPadding_.x);
        const int rightPadding = std::max(0, textPadding_.width);
        textRect.x += leftPadding;
        textRect.width = std::max(0, textRect.width - leftPadding - rightPadding);

        dc.SetTextForeground(text);
        dc.SetBackgroundMode(wxTRANSPARENT);
        if (manager_) dc.SetFont(manager_->GetUIFont());
        dc.DrawLabel(Utf8ToWxString(text_), textRect,
                     wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
        return;
    }

    // When selected, use selectedBkColor if set, then draw text with selectedTextColor.
    if (selected_) {
        const ImageSpec* img = selectedImage_.IsEmpty() ? &normalImage_ : &selectedImage_;
        if (selectedBkColor_.IsOk()) {
            // Draw background manually then border.
            const wxColour origBk = bkColor_;
            bkColor_ = selectedBkColor_;
            DrawBkColor(dc);
            bkColor_ = origBk;
        } else {
            DrawBkColor(dc);
        }
        if (img && !img->IsEmpty() && manager_) {
            wxBitmap bmp = manager_->LoadBitmap(img->path);
            if (bmp.IsOk()) {
                dc.DrawBitmap(bmp, rect_.GetTopLeft());
            }
        } else {
            DrawBkImage(dc);
        }
        DrawBorder(dc);
        const wxColour tc = selectedTextColor_.IsOk()
                          ? selectedTextColor_
                          : ResolveControlTextColor(manager_, textColor_, enabled_);
        DrawTextContent(dc, rect_, text_, tc);
    } else {
        Button::DoPaint(dc, clipRect);
    }
}

void Option::SetSelected(bool s) {
    if (selected_ == s) return;
    selected_ = s;
    // Deselect sibling Options in the same group (basic: done in parent)
    if (selected_ && parent_ && !group_.empty()) {
        auto* container = dynamic_cast<Container*>(parent_);
        if (container) {
            for (auto& sibling : container->GetChildren()) {
                if (sibling.get() == this) continue;
                auto* opt = dynamic_cast<Option*>(sibling.get());
                if (opt && opt->GetGroup() == group_)
                    opt->SetSelected(false);
            }
        }
        FireNotify("selectchanged");
    }
    Invalidate();
}

void Option::OnButtonUp(const wxPoint& pt) {
    if (!enabled_) return;
    state_ = rect_.Contains(pt) ? ButtonState::Hot : ButtonState::Normal;
    Invalidate();
    if (rect_.Contains(pt)) {
        if (cancelSelected_ && selected_) {
            SetSelected(false);
        } else {
            SetSelected(true);
        }
        FireNotify("click");
    }
}

void Button::OnSetFocus() {
    state_ = ButtonState::Focused;
    Invalidate();
}

void Button::OnKillFocus() {
    state_ = ButtonState::Normal;
    Invalidate();
}

// ══════════════════════════════════════════════════════════════════════════
// Text (inherits Label, adds hyperlink hit-test placeholder)
// ══════════════════════════════════════════════════════════════════════════

void Text::SetAttribute(const std::string& key, const std::string& val) {
    Label::SetAttribute(key, val);
}

void Text::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Label::DoPaint(dc, clipRect);
}

void Text::OnButtonUp(const wxPoint& pt) {
    if (!enabled_) return;
    const std::string link = HitTestLink(pt);
    if (!link.empty())
        FireNotify("link", pt.x, pt.y, 0, link);
    else
        FireNotify("click", pt.x, pt.y);
}

void Text::OnMouseMove(const wxPoint& pt) {
    (void)pt;
}

std::string Text::HitTestLink(const wxPoint&) const {
    return {};
}

// ══════════════════════════════════════════════════════════════════════════
// Progress
// ══════════════════════════════════════════════════════════════════════════

void Progress::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "foreimage"||key=="foreImage") { foreImage_    = ParseImageSpec(val); return; }
    if (key == "hor")                         { hor_          = ParseBOOL(val);      return; }
    if (key == "stretchfore"||key=="stretchFore"||key=="isstretchfore"){ stretchFore_= ParseBOOL(val); return; }
    if (key == "min")                         { min_          = ParseINT(val);       return; }
    if (key == "max")                         { max_          = ParseINT(val);       return; }
    if (key == "value")                       { SetValue(ParseINT(val));             return; }
    Label::SetAttribute(key, val);
}

void Progress::SetValue(int v) {
    value_ = std::clamp(v, min_, max_);
    Invalidate();
}

void Progress::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Control::DoPaint(dc, clipRect);
    if (max_ <= min_) return;

    const float ratio = float(value_ - min_) / float(max_ - min_);
    wxRect foreRect = rect_;
    if (hor_)  foreRect.SetWidth( int(rect_.GetWidth()  * ratio) );
    else       foreRect.SetHeight(int(rect_.GetHeight() * ratio) );

    if (!foreImage_.IsEmpty() && manager_) {
        wxBitmap bmp = manager_->LoadBitmap(foreImage_.path);
        if (bmp.IsOk()) {
            if (stretchFore_) {
                wxImage img = bmp.ConvertToImage()
                                 .Scale(foreRect.GetWidth(), foreRect.GetHeight());
                dc.DrawBitmap(wxBitmap(img), foreRect.GetTopLeft());
            } else {
                dc.DrawBitmap(bmp, foreRect.GetTopLeft());
            }
        }
    } else {
        const wxColour foreColor = textColor_.IsOk() ? textColor_ : *wxBLUE;
        if (textColor1_.IsOk()) {
            dc.GradientFillLinear(foreRect, foreColor, textColor1_,
                                  hor_ ? wxEAST : wxSOUTH);
        } else {
            dc.SetBrush(wxBrush(foreColor));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(foreRect);
        }
    }

    DrawBorder(dc);
    DrawTextContent(dc, rect_, text_, textColor_);
}

// ══════════════════════════════════════════════════════════════════════════
// Slider
// ══════════════════════════════════════════════════════════════════════════

void Slider::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "thumbimage"||key=="thumbImage")     { thumbImage_=ParseImageSpec(val);    return; }
    if (key == "thumbhotimage"||key=="thumbHotImage"){ thumbHotImage_=ParseImageSpec(val);return; }
    if (key == "thumbpushedimage"||key=="thumbPushedImage")
                                                    { thumbPushedImage_=ParseImageSpec(val);return; }
    if (key == "thumbsize"||key=="thumbSize")       { thumbSize_=ParseSIZE(val);          return; }
    if (key == "step")                              { step_=ParseINT(val,1);              return; }
    Progress::SetAttribute(key, val);
}

wxRect Slider::GetThumbRect() const {
    const float ratio = (max_ > min_)
        ? float(value_ - min_) / float(max_ - min_) : 0.f;
    if (hor_) {
        int cx = rect_.x + int(rect_.GetWidth() * ratio);
        int tw = thumbSize_.x > 0 ? thumbSize_.x : 8;
        int th = thumbSize_.y > 0 ? thumbSize_.y : rect_.GetHeight();
        return wxRect(cx - tw/2, rect_.y, tw, th);
    } else {
        int cy = rect_.y + int(rect_.GetHeight() * ratio);
        int tw = thumbSize_.x > 0 ? thumbSize_.x : rect_.GetWidth();
        int th = thumbSize_.y > 0 ? thumbSize_.y : 8;
        return wxRect(rect_.x, cy - th/2, tw, th);
    }
}

void Slider::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Progress::DoPaint(dc, clipRect);
    // Draw thumb
    const wxRect tr = GetThumbRect();
    const ImageSpec* img = dragging_ ? &thumbPushedImage_
                         : thumbHot_ ? &thumbHotImage_
                         :             &thumbImage_;
    if (img && !img->IsEmpty() && manager_) {
        wxBitmap bmp = manager_->LoadBitmap(img->path);
        if (bmp.IsOk()) {
            dc.DrawBitmap(bmp, tr.GetTopLeft());
            return;
        }
    }
    // Fallback thumb
    dc.SetBrush(*wxLIGHT_GREY_BRUSH);
    dc.SetPen(*wxGREY_PEN);
    dc.DrawRectangle(tr);
}

void Slider::OnButtonDown(const wxPoint& pt) {
    if (!enabled_) return;
    dragging_ = true;
    UpdateValueFromPoint(pt);
    Invalidate();
}

void Slider::OnButtonUp(const wxPoint& pt) {
    if (!enabled_) return;
    dragging_ = false;
    UpdateValueFromPoint(pt);
    Invalidate();
    FireNotify("valuechanged");
}

void Slider::OnMouseMove(const wxPoint& pt) {
    thumbHot_ = GetThumbRect().Contains(pt);
    if (dragging_) {
        UpdateValueFromPoint(pt);
        Invalidate();
    }
}

void Slider::OnMouseLeave() {
    thumbHot_ = false;
    dragging_ = false;
    Invalidate();
}

void Slider::UpdateValueFromPoint(const wxPoint& pt) {
    if (max_ <= min_) return;
    float ratio;
    if (hor_)  ratio = float(pt.x - rect_.x) / float(rect_.GetWidth());
    else       ratio = float(pt.y - rect_.y) / float(rect_.GetHeight());
    ratio = std::clamp(ratio, 0.f, 1.f);
    int newVal = min_ + int(ratio * float(max_ - min_));
    if (step_ > 1) newVal = (newVal / step_) * step_;
    SetValue(newVal);
    FireNotify("valuechanged");
}

// ══════════════════════════════════════════════════════════════════════════
// Edit
// ══════════════════════════════════════════════════════════════════════════

Edit::Edit() {
    textPadding_ = wxRect(4, 0, 4, 0);
}

Edit::~Edit() {
    // textCtrl_ is a wxWindow child of manager_; wxWidgets handles its lifetime
}

void Edit::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "hint") { SetHint(val); return; }
    if (key == "readonly"  || key == "readOnly") { readOnly_ = ParseBOOL(val); if (textCtrl_) textCtrl_->SetEditable(!readOnly_); return; }
    if (key == "password")                       { password_ = ParseBOOL(val);    return; }
    if (key == "multiline")                      { multiline_= ParseBOOL(val);    return; }
    if (key == "maxchar"   || key == "maxChar")  { maxChar_  = ParseINT(val); SyncNativeCtrl(); return; }
    if (key == "normalimage"||key=="normalImage"){ normalImage_=ParseImageSpec(val);return; }
    if (key == "hotimage"  ||key=="hotImage")    { hotImage_=ParseImageSpec(val); return; }
    if (key == "focusedimage"||key=="focusedImage"){ focusedImage_=ParseImageSpec(val);return; }
    if (key == "disabledimage"||key=="disabledImage"){disabledImage_=ParseImageSpec(val);return;}
    if (key == "nativebkcolor"||key=="nativeBkColor"){ nativeBkColor_=ParseDWORD(val); SyncNativeCtrl(); return; }
    Label::SetAttribute(key, val);
    if (textCtrl_) {
        if (key == "textpadding" || key == "textPadding") {
            SetRect(rect_);
        }
        if (key == "text") textCtrl_->SetValue(Utf8ToWxString(text_));
        SyncNativeCtrl();
    }
}

void Edit::OnManagerSet() {
    CreateNativeCtrl();
}

void Edit::CreateNativeCtrl() {
    if (textCtrl_ || !manager_) return;

    long style = wxBORDER_NONE | wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB;
    if (password_)  style |= wxTE_PASSWORD;
    if (multiline_) {
        style |= wxTE_MULTILINE;
#if defined(__WXGTK__)
        // wxGTK word wrapping keeps long tokens such as JWT/userSig on one
        // visual line. Character wrapping matches the Windows/macOS behavior.
        style |= wxTE_CHARWRAP;
#else
        style |= wxTE_WORDWRAP;
#endif
    }
    if (readOnly_)  style |= wxTE_READONLY;

    textCtrl_ = new wxTextCtrl(manager_, wxID_ANY,
                               Utf8ToWxString(text_), rect_.GetTopLeft(),
                               rect_.GetSize(), style);
    DisableTextCtrlSmartSubstitutions(textCtrl_);
    BindTextCtrlEditShortcuts(textCtrl_);
    textCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        event.Skip();
        if (textCtrl_) {
            text_ = WxStringToUtf8(textCtrl_->GetValue());
        }
        FireNotify("valuechanged");
    });
    textCtrl_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        if (enabled_) event.Skip();
    });
    textCtrl_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (enabled_) event.Skip();
    });
    textCtrl_->Bind(wxEVT_CHAR, [this](wxKeyEvent& event) {
        if (enabled_) event.Skip();
    });
    textCtrl_->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) { Invalidate(); event.Skip(); });
    textCtrl_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) { Invalidate(); event.Skip(); });
    SetRect(rect_);
    SyncNativeCtrl();
}

void Edit::SetRect(const wxRect& rc) {
    Control::SetRect(rc);
    if (textCtrl_) {
        const auto font = ResolveFont();
        if (textCtrl_->GetFont() != font) textCtrl_->SetFont(font);
        const int roundInset = borderRound_.x > 0
            ? std::max(1, std::min(4, borderRound_.x / 2))
            : 0;
        const int inset = std::max(borderSize_ > 0 ? borderSize_ : 0,
                                   roundInset);
        constexpr int kMinSingleLineCaretInset = 4;
        const int leftPadding =
            !multiline_ ? std::max(kMinSingleLineCaretInset, textPadding_.x)
                        : std::max(0, textPadding_.x);
        const int contentX = rc.x + inset;
        const int contentY = rc.y + inset;
        const int contentWidth = std::max(0, rc.width - inset * 2);
        const int contentHeight = std::max(0, rc.height - inset * 2);
        int nativeHeight = contentHeight;
        if (!multiline_) {
            // Use the native font/theme minimum on every port. The outer edit
            // supplies the border; the native text box is centred inside it.
            const int preferred = std::max(textCtrl_->GetBestSize().y,
                textCtrl_->GetCharHeight() + textCtrl_->FromDIP(4));
            nativeHeight = std::min(contentHeight, preferred);
        }
        const int topOffset = !multiline_
            ? std::max(0, (contentHeight - nativeHeight) / 2)
            : 0;
        if (nativeMargins_ != wxPoint(leftPadding, 0)) {
            textCtrl_->SetMargins(leftPadding, 0);
            nativeMargins_ = wxPoint(leftPadding, 0);
        }
        const wxRect nativeRect(contentX, contentY + topOffset, contentWidth, nativeHeight);
        if (textCtrl_->GetRect() != nativeRect) textCtrl_->SetSize(nativeRect);
        textCtrl_->Show(IsNativeWindowVisible());
    }
}

void Edit::OnSetFocus() {
    Label::OnSetFocus();
    if (!enabled_ && textCtrl_ && textCtrl_->HasFocus() && manager_) {
        manager_->SetFocus();
        return;
    }
    if (enabled_ && textCtrl_ && textCtrl_->IsShown() &&
        textCtrl_->IsEnabled()) {
        textCtrl_->SetFocus();
    }
}

void Edit::DoPaint(wxDC& dc, const wxRect& clipRect) {
    // The native wxTextCtrl paints itself; just draw background and border.
    Control::DoPaint(dc, clipRect);
    if (enabled_ && textCtrl_ && textCtrl_->HasFocus() && focusBorderColor_.IsOk()) {
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(focusBorderColor_, 1));
        dc.DrawRectangle(rect_);
    }
}

wxString Edit::GetValue() const {
    return textCtrl_ ? textCtrl_->GetValue() : Utf8ToWxString(text_);
}

void Edit::SetValue(const wxString& v) {
    text_ = WxStringToUtf8(v);
    if (textCtrl_) textCtrl_->SetValue(v);
}

void Edit::SetEnabled(bool e) {
    Control::SetEnabled(e);
    SyncNativeCtrl();
}

void Edit::SyncNativeCtrl() {
    if (!textCtrl_) return;
    textCtrl_->SetToolTip(Utf8ToWxString(tooltip_));
    textCtrl_->SetHint(Utf8ToWxString(hint_));
    // Keep the native control enabled even for logical-disabled edits.
    // wxTextCtrl lets the OS repaint real disabled fields with system colours
    // on Windows, which breaks dark themed forms. Read-only preserves the
    // custom colours while still preventing user edits.
    textCtrl_->Enable(true);
    textCtrl_->SetEditable(enabled_ && !readOnly_);

    const wxColour bg = nativeBkColor_.IsOk() ? nativeBkColor_ : bkColor_;
    if (bg.IsOk() && bg.Alpha() != 0) {
        textCtrl_->SetOwnBackgroundColour(bg);
        textCtrl_->SetBackgroundColour(bg);
    }

    const wxColour fg = ResolveControlTextColor(
        manager_, enabled_ ? textColor_ : disabledTextColor_, enabled_);
    if (fg.IsOk()) {
        textCtrl_->SetOwnForegroundColour(fg);
        textCtrl_->SetForegroundColour(fg);
    }

    // Font changes can alter the native minimum height even without a resize.
    SetRect(rect_);
    textCtrl_->SetMaxLength(static_cast<unsigned long>(std::max(0, maxChar_)));
    textCtrl_->Refresh();
    textCtrl_->Show(IsNativeWindowVisible());
}

void Edit::SetReadOnly(bool r) {
    readOnly_ = r;
    SyncNativeCtrl();
}

void Edit::SetHint(std::string_view hint) {
    hint_ = hint;
    if (textCtrl_) textCtrl_->SetHint(Utf8ToWxString(hint_));
}

void Edit::SetPassword(bool p) {
    password_ = p;
    // Password style cannot be changed at runtime in wxTextCtrl;
    // recreate if needed (simplify: mark flag, recreate on next show)
}

void Edit::SetMaxChar(int n) {
    maxChar_ = std::max(0, n);
    if (textCtrl_) textCtrl_->SetMaxLength(static_cast<unsigned long>(maxChar_));
}

} // namespace wxui
