#include <libwxui.hpp>

#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/panel.h>
#include <wx/popupwin.h>

#include <algorithm>

namespace wxui {

namespace {

bool HasVisibleColor(const wxColour& color) {
    return color.IsOk() && color.Alpha() != 0;
}

wxColour PickColor(const wxColour& color, const wxColour& fallback) {
    return HasVisibleColor(color) ? color : fallback;
}

wxColour ResolveTextColor(const UIManager* manager,
                          const wxColour& requested,
                          bool enabled) {
    if (HasVisibleColor(requested)) return requested;
    if (manager && manager->GetRoot()) {
        return enabled ? manager->GetRoot()->GetDefaultFontColor()
                       : manager->GetRoot()->GetDisabledFontColor();
    }
    return enabled ? wxColour(255, 255, 255) : wxColour(145, 145, 145);
}

wxColour ResolveBkColor(const Combo* combo,
                        const wxColour& requested,
                        const wxColour& fallback) {
    if (HasVisibleColor(requested)) return requested;
    if (combo && HasVisibleColor(combo->GetBkColor())) return combo->GetBkColor();
    return fallback;
}

int TextAlignFlag(int itemAlign) {
    return itemAlign == 2 ? wxALIGN_RIGHT : wxALIGN_LEFT;
}

void DrawChevron(wxDC& dc, const wxRect& rect, const wxColour& color, bool up) {
    const int cx = rect.x + rect.width / 2;
    const int cy = rect.y + rect.height / 2;
    const int half = 4;
    dc.SetPen(wxPen(color, 1));
    if (up) {
        dc.DrawLine(cx - half, cy + 2, cx, cy - 2);
        dc.DrawLine(cx, cy - 2, cx + half, cy + 2);
    } else {
        dc.DrawLine(cx - half, cy - 2, cx, cy + 2);
        dc.DrawLine(cx, cy + 2, cx + half, cy - 2);
    }
}

int ClampInt(int value, int minValue, int maxValue) {
    return std::min(std::max(value, minValue), maxValue);
}

} // namespace

class ComboPopupWindow final : public wxPopupTransientWindow {
public:
    ComboPopupWindow(wxWindow* parent, Combo* owner)
        : wxPopupTransientWindow(parent, wxBORDER_NONE)
        , owner_(owner) {}

private:
    void OnDismiss() override {
        if (owner_) owner_->OnPopupDismissed();
    }

    Combo* owner_ = nullptr;
};

class ComboPopupPanel final : public wxPanel {
public:
    ComboPopupPanel(wxWindow* parent, Combo* owner)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                  wxBORDER_NONE | wxWANTS_CHARS)
        , owner_(owner) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &ComboPopupPanel::OnPaint, this);
        Bind(wxEVT_MOTION, &ComboPopupPanel::OnMouseMove, this);
        Bind(wxEVT_LEAVE_WINDOW, &ComboPopupPanel::OnMouseLeave, this);
        Bind(wxEVT_LEFT_UP, &ComboPopupPanel::OnLeftUp, this);
        Bind(wxEVT_MOUSEWHEEL, &ComboPopupPanel::OnMouseWheel, this);
        Bind(wxEVT_KEY_DOWN, &ComboPopupPanel::OnKeyDown, this);
    }

private:
    int HitItem(const wxPoint& pt) const {
        if (!owner_) return -1;
        const wxRect client = GetClientRect();
        if (!client.Contains(pt) || pt.y < 1 || pt.y >= client.GetBottom()) {
            return -1;
        }

        const int row = (pt.y - 1) / owner_->GetPopupItemHeight();
        const int idx = owner_->popupScrollOffset_ + row;
        return idx >= 0 && idx < static_cast<int>(owner_->items_.size()) ? idx : -1;
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const wxRect client = GetClientRect();
        if (!owner_) {
            dc.Clear();
            return;
        }

        const wxColour bg = ResolveBkColor(owner_, owner_->itemBkColor_,
                                           wxColour(32, 32, 32));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(bg));
        dc.DrawRectangle(client);

        if (owner_->manager_) dc.SetFont(owner_->manager_->GetUIFont());
        dc.SetBackgroundMode(wxTRANSPARENT);

        const int itemHeight = owner_->GetPopupItemHeight();
        const int rowCount = owner_->GetPopupVisibleRows();
        const bool needsScroll =
            static_cast<int>(owner_->items_.size()) > rowCount;
        const int scrollWidth = needsScroll ? 5 : 0;

        for (int row = 0; row < rowCount; ++row) {
            const int idx = owner_->popupScrollOffset_ + row;
            if (idx < 0 || idx >= static_cast<int>(owner_->items_.size())) {
                break;
            }

            wxRect rowRect(1, 1 + row * itemHeight,
                           std::max(0, client.width - 2 - scrollWidth),
                           itemHeight);
            const bool selected = idx == owner_->selection_;
            const bool hot = idx == owner_->hotIndex_;

            wxColour rowBk = bg;
            if (selected) {
                rowBk = PickColor(owner_->itemSelBkColor_, wxColour(70, 54, 137));
            } else if (hot) {
                rowBk = PickColor(owner_->itemHotBkColor_, wxColour(48, 48, 48));
            }
            if (HasVisibleColor(rowBk)) {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(rowBk));
                dc.DrawRectangle(rowRect);
            }

            wxColour textColor =
                ResolveTextColor(owner_->manager_, owner_->itemTextColor_, true);
            if (selected) {
                textColor = ResolveTextColor(owner_->manager_,
                                             owner_->itemSelTextColor_, true);
            } else if (hot) {
                textColor = ResolveTextColor(owner_->manager_,
                                             owner_->itemHotTextColor_, true);
            }

            const int leftPad = std::max(8, owner_->itemTextPadding_.x);
            const int rightPad = std::max(8, owner_->itemTextPadding_.width);
            wxRect textRect(rowRect.x + leftPad, rowRect.y,
                            std::max(0, rowRect.width - leftPad - rightPad),
                            rowRect.height);
            dc.SetTextForeground(textColor);
            dc.DrawLabel(owner_->items_[idx], textRect,
                         wxALIGN_CENTER_VERTICAL |
                         TextAlignFlag(owner_->itemAlign_) |
                         wxELLIPSIZE_END);
        }

        if (needsScroll) {
            const int trackX = client.GetRight() - scrollWidth + 1;
            wxRect track(trackX, 1, scrollWidth - 1,
                         std::max(0, client.height - 2));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(wxColour(28, 28, 28)));
            dc.DrawRectangle(track);

            const int total = static_cast<int>(owner_->items_.size());
            const int maxOffset = std::max(1, total - rowCount);
            const int thumbHeight =
                std::max(18, track.height * rowCount / std::max(rowCount, total));
            const int thumbTop = track.y +
                (track.height - thumbHeight) * owner_->popupScrollOffset_ / maxOffset;
            dc.SetBrush(wxBrush(wxColour(88, 88, 88)));
            dc.DrawRectangle(track.x, thumbTop, track.width, thumbHeight);
        }

        const wxColour border = PickColor(owner_->borderColor_,
                                          wxColour(64, 64, 64));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(border, 1));
        dc.DrawRectangle(client);
    }

    void OnMouseMove(wxMouseEvent& event) {
        if (!owner_) return;
        const int idx = HitItem(event.GetPosition());
        if (idx != owner_->hotIndex_) {
            owner_->hotIndex_ = idx;
            Refresh();
        }
    }

    void OnMouseLeave(wxMouseEvent&) {
        if (!owner_) return;
        if (owner_->hotIndex_ != owner_->selection_) {
            owner_->hotIndex_ = owner_->selection_;
            Refresh();
        }
    }

    void OnLeftUp(wxMouseEvent& event) {
        if (!owner_) return;
        const int idx = HitItem(event.GetPosition());
        if (idx >= 0) {
            owner_->OnPopupSelect(idx);
        } else {
            owner_->HideDropdown();
        }
    }

    void OnMouseWheel(wxMouseEvent& event) {
        if (!owner_) return;
        const int rows = owner_->GetPopupVisibleRows();
        const int maxOffset =
            std::max(0, static_cast<int>(owner_->items_.size()) - rows);
        const int step = event.GetWheelRotation() > 0 ? -1 : 1;
        const int next = ClampInt(owner_->popupScrollOffset_ + step,
                                  0, maxOffset);
        if (next != owner_->popupScrollOffset_) {
            owner_->popupScrollOffset_ = next;
            Refresh();
        }
    }

    void OnKeyDown(wxKeyEvent& event) {
        if (!owner_) {
            event.Skip();
            return;
        }

        const int count = static_cast<int>(owner_->items_.size());
        if (count <= 0) {
            event.Skip();
            return;
        }

        switch (event.GetKeyCode()) {
            case WXK_ESCAPE:
                owner_->HideDropdown();
                return;
            case WXK_RETURN:
            case WXK_NUMPAD_ENTER:
                if (owner_->hotIndex_ >= 0 && owner_->hotIndex_ < count) {
                    owner_->OnPopupSelect(owner_->hotIndex_);
                }
                return;
            case WXK_UP:
                owner_->hotIndex_ = ClampInt(owner_->hotIndex_ < 0
                                                 ? owner_->selection_
                                                 : owner_->hotIndex_ - 1,
                                             0, count - 1);
                owner_->EnsurePopupHotVisible();
                Refresh();
                return;
            case WXK_DOWN:
                owner_->hotIndex_ = ClampInt(owner_->hotIndex_ < 0
                                                 ? owner_->selection_
                                                 : owner_->hotIndex_ + 1,
                                             0, count - 1);
                owner_->EnsurePopupHotVisible();
                Refresh();
                return;
            case WXK_HOME:
                owner_->hotIndex_ = 0;
                owner_->EnsurePopupHotVisible();
                Refresh();
                return;
            case WXK_END:
                owner_->hotIndex_ = count - 1;
                owner_->EnsurePopupHotVisible();
                Refresh();
                return;
            default:
                event.Skip();
                return;
        }
    }

    Combo* owner_ = nullptr;
};

Combo::~Combo() {
    HideDropdown();
}

void Combo::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "items") {
        Clear();
        std::size_t start = 0;
        while (start <= val.size()) {
            const std::size_t end = val.find('|', start);
            const std::string item =
                end == std::string::npos ? val.substr(start)
                                         : val.substr(start, end - start);
            if (!item.empty()) AddItem(std::string_view(item));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (selection_ < 0 && !items_.empty()) SetSelection(0);
        SyncNativeItems();
        return;
    }
    if (key == "selected" || key == "select" || key == "selection")
        { SetSelection(ParseINT(val, selection_)); return; }
    if (key == "textpadding"   || key == "textPadding")
        { textPadding_   = ParseRECT(val); SyncNativeCtrl(); return; }
    if (key == "dropboxsize"   || key == "dropBoxSize")
        { dropBoxSize_   = ParseSIZE(val); return; }
    if (key == "itemfont"      || key == "itemFont")
        { itemFontId_    = ParseINT(val);  return; }
    if (key == "itemalign"     || key == "itemAlign")
        { itemAlign_     = ParseINT(val);  return; }
    if (key == "itemtextpadding"||key == "itemTextPadding")
        { itemTextPadding_= ParseRECT(val);return; }
    if (key == "itemtextcolor" || key == "itemTextColor")
        { itemTextColor_ = ParseDWORD(val); SyncNativeCtrl(); return; }
    if (key == "textcolor" || key == "textColor")
        { itemTextColor_ = ParseDWORD(val); SyncNativeCtrl(); return; }
    if (key == "itembkcolor"   || key == "itemBkColor")
        { itemBkColor_   = ParseDWORD(val); SyncNativeCtrl(); return; }
    if (key == "itemseltextcolor"||key == "itemSelTextColor")
        { itemSelTextColor_= ParseDWORD(val); SyncNativeCtrl(); return; }
    if (key == "itemselbkcolor"||key == "itemSelBkColor")
        { itemSelBkColor_  = ParseDWORD(val); SyncNativeCtrl(); return; }
    if (key == "itemhottextcolor"||key == "itemHotTextColor")
        { itemHotTextColor_= ParseDWORD(val); SyncNativeCtrl(); return; }
    if (key == "itemhotbkcolor"||key == "itemHotBkColor")
        { itemHotBkColor_  = ParseDWORD(val); SyncNativeCtrl(); return; }
    if (key == "itemdistextcolor"||key == "itemDisTextColor")
        { itemDisTextColor_= ParseDWORD(val); SyncNativeCtrl(); return; }
    if (key == "multiexpanding"||key == "multiExpanding")
        { multiExpanding_= ParseBOOL(val); return; }
    if (key == "normalimage"  ||key == "normalImage")
        { normalImage_   = ParseImageSpec(val); return; }
    if (key == "hotimage"     ||key == "hotImage")
        { hotImage_      = ParseImageSpec(val); return; }
    if (key == "pushedimage"  ||key == "pushedImage")
        { pushedImage_   = ParseImageSpec(val); return; }
    if (key == "disabledimage"||key == "disabledImage")
        { disabledImage_ = ParseImageSpec(val); return; }
    Container::SetAttribute(key, val);
    SyncNativeCtrl();
}

void Combo::DoPaint(wxDC& dc, const wxRect& clipRect) {
    (void)clipRect;
    Control::DoPaint(dc, clipRect);

    if (manager_) dc.SetFont(manager_->GetUIFont());
    dc.SetBackgroundMode(wxTRANSPARENT);

    const wxColour textColor = enabled_
        ? ResolveTextColor(manager_, itemTextColor_, true)
        : ResolveTextColor(manager_, itemDisTextColor_, false);

    const int borderInset = borderSize_ > 0 ? borderSize_ : 0;
    const int arrowWidth = std::max(22, std::min(30, rect_.height));
    wxRect textRect(rect_.x + borderInset + std::max(0, textPadding_.x),
                    rect_.y + borderInset + std::max(0, textPadding_.y),
                    std::max(0, rect_.width - borderInset * 2 -
                                std::max(0, textPadding_.x) -
                                std::max(0, textPadding_.width) - arrowWidth),
                    std::max(0, rect_.height - borderInset * 2 -
                                std::max(0, textPadding_.y) -
                                std::max(0, textPadding_.height)));
    dc.SetTextForeground(textColor);
    dc.DrawLabel(GetSelectedText(), textRect,
                 wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxELLIPSIZE_END);

    wxRect arrowRect(rect_.x + rect_.width - arrowWidth, rect_.y + borderInset,
                     arrowWidth, std::max(0, rect_.height - borderInset * 2));
    const wxColour lineColor = PickColor(borderColor_, wxColour(72, 72, 72));
    if (arrowRect.height > 8) {
        dc.SetPen(wxPen(lineColor, 1));
        dc.DrawLine(arrowRect.x, arrowRect.y + 5,
                    arrowRect.x, arrowRect.GetBottom() - 4);
    }
    DrawChevron(dc, arrowRect, textColor, dropped_);
}

void Combo::OnManagerSet() {
    Container::OnManagerSet();
    CreateNativeCtrl();
}

void Combo::SetRect(const wxRect& rc) {
    Container::SetRect(rc);
    if (dropped_) HideDropdown();
    SyncNativeCtrl();
}

void Combo::SetVisible(bool v) {
    Container::SetVisible(v);
    if (!v) HideDropdown();
    SyncNativeCtrl();
}

void Combo::SetEnabled(bool e) {
    Container::SetEnabled(e);
    if (!e) HideDropdown();
    SyncNativeCtrl();
}

void Combo::OnButtonDown(const wxPoint& pt) {
    if (!enabled_) return;
    (void)pt;
    if (manager_) manager_->SetFocus();
    if (dropped_) HideDropdown();
}

void Combo::OnButtonUp(const wxPoint& pt) {
    if (!enabled_ || dropped_) return;
    if (rect_.Contains(pt)) ShowDropdown();
}

void Combo::OnMouseLeave() {
    // Keep the popup open while the user moves from the control to the popup.
}

bool Combo::OnKeyDown(int keyCode) {
    if (!enabled_) return false;
    if (keyCode == WXK_SPACE || keyCode == WXK_RETURN ||
        keyCode == WXK_NUMPAD_ENTER || keyCode == WXK_DOWN) {
        if (dropped_) HideDropdown(); else ShowDropdown();
        return true;
    }
    return false;
}

void Combo::AddItem(const wxString& text) {
    items_.push_back(text);
    SyncNativeItems();
}

void Combo::InsertItem(int idx, const wxString& text) {
    if (idx < 0 || idx > static_cast<int>(items_.size())) {
        idx = static_cast<int>(items_.size());
    }
    items_.insert(items_.begin() + idx, text);
    SyncNativeItems();
}

void Combo::RemoveItem(int idx) {
    if (idx < 0 || idx >= static_cast<int>(items_.size())) return;
    items_.erase(items_.begin() + idx);
    if (selection_ >= static_cast<int>(items_.size())) {
        selection_ = static_cast<int>(items_.size()) - 1;
    }
    SyncNativeItems();
}

void Combo::Clear() {
    items_.clear();
    selection_ = -1;
    hotIndex_ = -1;
    popupScrollOffset_ = 0;
    SyncNativeItems();
}

void Combo::SetSelection(int idx, bool notify) {
    if (idx < -1 || idx >= static_cast<int>(items_.size())) return;
    selection_ = idx;
    hotIndex_ = selection_;
    Invalidate();
    if (popupPanel_) popupPanel_->Refresh();
    if (notify) FireNotify("selchanged", 0, 0, selection_, GetSelectedTextUtf8());
}

int Combo::GetItemCount() const {
    return static_cast<int>(items_.size());
}

wxString Combo::GetItemText(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(items_.size())) return {};
    return items_[idx];
}

wxString Combo::GetSelectedText() const {
    if (selection_ < 0 || selection_ >= static_cast<int>(items_.size())) return {};
    return items_[selection_];
}

void Combo::CreateNativeCtrl() {
    // Combo is fully owner-drawn; kept for compatibility with older call sites.
}

void Combo::SyncNativeCtrl() {
    Invalidate();
    if (popupPanel_) popupPanel_->Refresh();
}

void Combo::SyncNativeItems() {
    if (selection_ >= static_cast<int>(items_.size())) {
        selection_ = static_cast<int>(items_.size()) - 1;
    }
    if (selection_ < 0 && !items_.empty()) {
        selection_ = 0;
    }
    if (items_.empty()) {
        selection_ = -1;
        hotIndex_ = -1;
        popupScrollOffset_ = 0;
        HideDropdown();
    } else {
        hotIndex_ = selection_;
        EnsurePopupHotVisible();
    }
    Invalidate();
    if (popupPanel_) popupPanel_->Refresh();
}

int Combo::GetPopupItemHeight() const {
    const int textHeight = manager_ ? manager_->GetCharHeight() : 14;
    return std::max(24, textHeight + 10);
}

int Combo::GetPopupVisibleRows() const {
    const int itemHeight = GetPopupItemHeight();
    if (popupPanel_) {
        return std::max(1, (popupPanel_->GetClientSize().y - 2) / itemHeight);
    }
    const int maxHeight = dropBoxSize_.y > 0 ? dropBoxSize_.y : 150;
    return std::max(1, (maxHeight - 2) / itemHeight);
}

void Combo::EnsurePopupHotVisible() {
    if (items_.empty() || hotIndex_ < 0) return;
    const int rows = GetPopupVisibleRows();
    const int maxOffset = std::max(0, static_cast<int>(items_.size()) - rows);
    if (hotIndex_ < popupScrollOffset_) {
        popupScrollOffset_ = hotIndex_;
    } else if (hotIndex_ >= popupScrollOffset_ + rows) {
        popupScrollOffset_ = hotIndex_ - rows + 1;
    }
    popupScrollOffset_ = ClampInt(popupScrollOffset_, 0, maxOffset);
}

void Combo::ShowDropdown() {
    if (!manager_ || items_.empty() || !enabled_) return;
    if (popup_) HideDropdown();

    const int itemHeight = GetPopupItemHeight();
    const int itemCount = static_cast<int>(items_.size());
    const int maxHeight = std::max(itemHeight + 2,
                                   dropBoxSize_.y > 0 ? dropBoxSize_.y : 150);
    const int naturalHeight = itemCount * itemHeight + 2;
    int popupHeight = std::min(naturalHeight, maxHeight);
    int popupWidth = dropBoxSize_.x > 0 ? std::max(rect_.width, dropBoxSize_.x)
                                        : rect_.width;
    popupWidth = std::max(80, popupWidth);

    wxPoint pos = manager_->ClientToScreen(wxPoint(rect_.x, rect_.GetBottom() + 1));
    const int displayIndex = wxDisplay::GetFromWindow(manager_);
    if (displayIndex != wxNOT_FOUND) {
        const wxRect workArea = wxDisplay(displayIndex).GetClientArea();
        const int below = workArea.GetBottom() - pos.y + 1;
        const int above = manager_->ClientToScreen(rect_.GetTopLeft()).y - workArea.y;
        if (popupHeight > below && above > below) {
            popupHeight = std::min(popupHeight, std::max(itemHeight + 2, above));
            pos.y = manager_->ClientToScreen(rect_.GetTopLeft()).y - popupHeight - 1;
        } else {
            popupHeight = std::min(popupHeight, std::max(itemHeight + 2, below));
        }
        pos.x = ClampInt(pos.x, workArea.x,
                         std::max(workArea.x, workArea.GetRight() - popupWidth + 1));
    }

    popup_ = new ComboPopupWindow(manager_, this);
    popupPanel_ = new ComboPopupPanel(popup_, this);
    popup_->SetClientSize(wxSize(popupWidth, popupHeight));
    popupPanel_->SetSize(popup_->GetClientRect());
    popup_->Move(pos);

    hotIndex_ = selection_ >= 0 ? selection_ : 0;
    const int rows = GetPopupVisibleRows();
    const int maxOffset = std::max(0, itemCount - rows);
    popupScrollOffset_ = ClampInt(hotIndex_ - rows / 2, 0, maxOffset);

    dropped_ = true;
    Invalidate();
    FireNotify("dropdown");
    popup_->Popup(popupPanel_);
    popupPanel_->SetFocus();
    popupPanel_->Refresh();
}

void Combo::HideDropdown() {
    const bool wasDropped = dropped_;
    wxPopupTransientWindow* popup = popup_;
    popup_ = nullptr;
    popupPanel_ = nullptr;
    dropped_ = false;
    hotIndex_ = selection_;
    popupScrollOffset_ = 0;

    if (popup) {
        popup->Hide();
        popup->Destroy();
    }

    Invalidate();
    if (wasDropped) FireNotify("dropdownhide");
}

void Combo::OnPopupDismissed() {
    if (!popup_ && !dropped_) return;

    wxPopupTransientWindow* popup = popup_;
    popup_ = nullptr;
    popupPanel_ = nullptr;
    const bool wasDropped = dropped_;
    dropped_ = false;
    hotIndex_ = selection_;
    popupScrollOffset_ = 0;

    if (popup) popup->Destroy();
    Invalidate();
    if (wasDropped) FireNotify("dropdownhide");
}

void Combo::OnPopupSelect(int idx) {
    if (idx < 0 || idx >= static_cast<int>(items_.size())) return;
    SetSelection(idx, true);
    HideDropdown();
}

} // namespace wxui
