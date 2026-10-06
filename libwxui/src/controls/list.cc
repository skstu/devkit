#include <libwxui.hpp>
#include <libwxui/appearance.hpp>

#include <wx/log.h>
#include <algorithm>
#include <string>

namespace wxui {

namespace {

constexpr int kMinHeaderColumnWidth = 48;
constexpr int kListScrollBarWidth = 12;
constexpr int kListScrollBarMargin = 3;
constexpr int kListScrollThumbMinHeight = 22;
constexpr int kListWheelRows = 3;

ListHeader* FindHeaderChild(const Container::ChildList& children) {
    for (const auto& child : children) {
        if (auto* header = dynamic_cast<ListHeader*>(child.get())) {
            return header;
        }
    }
    return nullptr;
}

wxColour RootDefaultText(const UIManager* manager) {
    if (manager && manager->GetRoot()) return manager->GetRoot()->GetDefaultFontColor();
    return *wxBLACK;
}

wxColour ResolveListTextColor(const ListLabelElement* item) {
    const auto* list = item ? dynamic_cast<const List*>(item->GetOwnerList()) : nullptr;
    if (item && item->GetTextColor().IsOk()) return item->GetTextColor();
    if (!list) return RootDefaultText(item ? item->GetManager() : nullptr);
    if (item->IsSelected() && list->itemSelTextColor_.IsOk()) return list->itemSelTextColor_;
    if (!item->IsSelected() && list->itemTextColor_.IsOk()) return list->itemTextColor_;
    return RootDefaultText(item->GetManager());
}

wxColour ResolveListBkColor(const ListLabelElement* item, bool hot) {
    const auto* list = item ? dynamic_cast<const List*>(item->GetOwnerList()) : nullptr;
    if (!list) {
        return item && item->IsSelected() ? wxColour(0xBA, 0xE4, 0xFF)
                                          : wxColour(0xD8, 0xF0, 0xFF);
    }
    if (item->IsSelected()) {
        if (list->itemSelBkColor_.IsOk()) return list->itemSelBkColor_;
        if (item->GetManager() && item->GetManager()->GetRoot())
            return item->GetManager()->GetRoot()->GetSelectedColor();
        return wxColour(0xBA, 0xE4, 0xFF);
    }
    if (hot && list->itemHotBkColor_.IsOk()) return list->itemHotBkColor_;
    if (list->itemBkColor_.IsOk()) return list->itemBkColor_;
    return wxNullColour;
}

wxColour ResolveListContainerBkColor(const ListContainerElement* item,
                                     bool hot) {
    const auto* list = item ? dynamic_cast<const List*>(item->GetOwnerList()) : nullptr;
    if (!list) {
        return item && item->IsSelected() ? wxColour(0xBA, 0xE4, 0xFF)
                                          : wxColour(0xD8, 0xF0, 0xFF);
    }
    if (item->IsSelected()) {
        if (list->itemSelBkColor_.IsOk()) return list->itemSelBkColor_;
        if (item->GetManager() && item->GetManager()->GetRoot())
            return item->GetManager()->GetRoot()->GetSelectedColor();
        return wxColour(0xBA, 0xE4, 0xFF);
    }
    if (hot && list->itemHotBkColor_.IsOk()) return list->itemHotBkColor_;
    if (list->itemBkColor_.IsOk()) return list->itemBkColor_;
    return wxNullColour;
}

std::vector<int> ResolveColumnWidths(const Control* item,
                                     const Control* ownerList,
                                     std::size_t columnCount) {
    if (columnCount == 0 || item == nullptr) {
        return {};
    }

    std::vector<int> widths;
    if (auto* list = dynamic_cast<const List*>(ownerList)) {
        if (auto* header = list->GetHeader()) {
            widths.reserve(header->ChildCount());
            for (const auto& child : header->GetChildren()) {
                if (!child->IsVisible()) {
                    continue;
                }
                const int width = child->GetRect().GetWidth();
                if (width > 0) {
                    widths.push_back(width);
                }
            }
        }
    }

    if (widths.empty()) {
        const int totalWidth = item->GetRect().GetWidth();
        const int baseWidth = columnCount > 0
            ? std::max(0, totalWidth / static_cast<int>(columnCount))
            : 0;
        widths.assign(columnCount, baseWidth);
        if (!widths.empty()) {
            widths.back() = std::max(0,
                totalWidth - baseWidth * static_cast<int>(columnCount - 1));
        }
        return widths;
    }

    if (widths.size() < columnCount) {
        const int fallback = widths.empty()
            ? 0
            : widths.back();
        widths.resize(columnCount, fallback);
    }
    return widths;
}

ListHeaderItem* PreviousVisibleHeaderItem(const ListHeaderItem* item) {
    auto* parent = item ? item->GetParent() : nullptr;
    if (!parent) return nullptr;

    ListHeaderItem* previous = nullptr;
    for (const auto& child : parent->GetChildren()) {
        auto* headerItem = dynamic_cast<ListHeaderItem*>(child.get());
        if (!headerItem || !headerItem->IsVisible()) continue;
        if (headerItem == item) return previous;
        previous = headerItem;
    }
    return nullptr;
}

bool HasNextVisibleHeaderItem(const ListHeaderItem* item) {
    auto* parent = item ? item->GetParent() : nullptr;
    if (!parent) return false;

    bool found = false;
    for (const auto& child : parent->GetChildren()) {
        auto* headerItem = dynamic_cast<ListHeaderItem*>(child.get());
        if (!headerItem || !headerItem->IsVisible()) continue;
        if (found) return true;
        if (headerItem == item) found = true;
    }
    return false;
}

int MinimumHeaderContentWidth(const ListHeader* header) {
    if (!header) return 0;

    int width = 0;
    int visibleCount = 0;
    for (const auto& child : header->GetChildren()) {
        if (!child->IsVisible()) continue;
        ++visibleCount;
        const wxSize fixed = child->GetFixedSize();
        const wxSize minimum = child->GetMinSize();
        width += fixed.x > 0 ? fixed.x : std::max(kMinHeaderColumnWidth, minimum.x);
    }
    if (visibleCount > 1) {
        width += header->GetChildPadding() * (visibleCount - 1);
    }
    return width;
}

int ResolveListContentWidth(const ListHeader* header,
                            int configuredWidth,
                            int viewportWidth) {
    return std::max({0, viewportWidth, configuredWidth,
                     MinimumHeaderContentWidth(header)});
}

ListHeaderItem* HeaderResizeTargetAt(ListHeaderItem* item, const wxPoint& pt) {
    if (!item || !item->IsDragable()) return nullptr;

    const wxRect rect = item->GetRect();
    const int sepWidth = std::max(2, item->GetSepWidth());
    const bool onLeft = pt.x >= rect.GetLeft() && pt.x < rect.GetLeft() + sepWidth;
    const bool onRight = pt.x <= rect.GetRight() && pt.x > rect.GetRight() - sepWidth;

    if (onLeft) {
        auto* previous = PreviousVisibleHeaderItem(item);
        return previous && previous->IsDragable() ? previous : nullptr;
    }
    if (onRight && HasNextVisibleHeaderItem(item)) {
        return item;
    }
    return nullptr;
}

int HeaderResizeMaximum(const ListHeaderItem* target) {
    auto* parent = target ? target->GetParent() : nullptr;
    if (!parent) return 2000;

    const wxRect parentRect = parent->GetRect();
    const wxRect targetRect = target->GetRect();
    const int available = std::max(0, parentRect.GetRight() - targetRect.GetLeft() + 1);

    bool afterTarget = false;
    int trailingMinimum = 0;
    int trailingCount = 0;
    for (const auto& child : parent->GetChildren()) {
        auto* headerItem = dynamic_cast<ListHeaderItem*>(child.get());
        if (!headerItem || !headerItem->IsVisible()) continue;
        if (afterTarget) {
            ++trailingCount;
            trailingMinimum +=
                std::max(kMinHeaderColumnWidth, headerItem->GetMinSize().x);
        }
        if (headerItem == target) afterTarget = true;
    }
    if (trailingCount > 0) {
        trailingMinimum += parent->GetChildPadding() * trailingCount;
    }
    return std::max(kMinHeaderColumnWidth, available - trailingMinimum);
}

void SetHorizontalResizeCursor(UIManager* manager) {
    if (manager) manager->SetCursor(wxCursor(wxCURSOR_SIZEWE));
}

void ResetCursor(UIManager* manager) {
    if (manager) manager->SetCursor(wxCursor(wxCURSOR_ARROW));
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// ListHeaderItem
// ══════════════════════════════════════════════════════════════════════════

void ListHeaderItem::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "align") { hAlign_ = val == "left" ? wxALIGN_LEFT : val == "right" ? wxALIGN_RIGHT : wxALIGN_CENTER_HORIZONTAL; Invalidate(); return; }
    if (key == "dragable")          { dragable_    = ParseBOOL(val); return; }
    if (key == "sepwidth")          { sepWidth_    = ParseINT(val);  return; }
    if (key == "normalimage")       { normalImage_ = ParseImageSpec(val); return; }
    if (key == "hotimage")          { hotImage_    = ParseImageSpec(val); return; }
    if (key == "pushedimage")       { pushedImage_ = ParseImageSpec(val); return; }
    if (key == "focusedimage")      { focusedImage_= ParseImageSpec(val); return; }
    if (key == "sepimage")          { sepImage_    = ParseImageSpec(val); return; }
    if (key == "textcolor")         { textColor_   = ParseDWORD(val); return; }
    Control::SetAttribute(key, val);
}

void ListHeaderItem::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Control::DoPaint(dc, clipRect);
    const ImageSpec* img = hot_ ? (pushed_ ? &pushedImage_ : &hotImage_) : &normalImage_;
    if (img && !img->IsEmpty() && manager_) {
        wxBitmap bmp = manager_->LoadBitmap(img->path);
        if (bmp.IsOk()) dc.DrawBitmap(bmp, rect_.GetTopLeft());
    }
    dc.SetTextForeground(textColor_.IsOk() ? textColor_ : RootDefaultText(manager_));
    if (manager_) dc.SetFont(manager_->GetUIFont());
    auto textRect = rect_; textRect.Deflate(5, 0);
    if (textRect.width > 0) dc.DrawLabel(wxControl::Ellipsize(Utf8ToWxString(text_), dc, wxELLIPSIZE_END, textRect.width), textRect, wxALIGN_CENTER_VERTICAL | hAlign_);
    if (dragable_ && HasNextVisibleHeaderItem(this)) {
        dc.SetPen(wxPen(wxColour(0x3A, 0x3A, 0x3A)));
        dc.DrawLine(rect_.GetRight(), rect_.GetTop() + 6,
                    rect_.GetRight(), rect_.GetBottom() - 6);
    }
}

void ListHeaderItem::OnMouseEnter(const wxPoint&) { hot_ = true;   Invalidate(); }
void ListHeaderItem::OnMouseLeave() {
    hot_ = false;
    pushed_ = false;
    if (!resizing_) ResetCursor(manager_);
    Invalidate();
}
void ListHeaderItem::OnMouseMove(const wxPoint& pt) {
    if (resizing_ && resizeTarget_) {
        const int requested = resizeStartWidth_ + (pt.x - resizeStartX_);
        resizeTarget_->SetFixedWidth(
            std::clamp(requested, kMinHeaderColumnWidth,
                       HeaderResizeMaximum(resizeTarget_)));
        SetHorizontalResizeCursor(manager_);
        if (manager_) {
            manager_->RequestLayout();
            manager_->Refresh();
        }
        return;
    }

    if (HeaderResizeTargetAt(this, pt)) {
        SetHorizontalResizeCursor(manager_);
    } else {
        ResetCursor(manager_);
    }
}
void ListHeaderItem::OnButtonDown(const wxPoint& pt) {
    if (auto* target = HeaderResizeTargetAt(this, pt)) {
        resizing_ = true;
        resizeTarget_ = target;
        resizeStartX_ = pt.x;
        resizeStartWidth_ = target->GetRect().GetWidth();
        pushed_ = false;
        SetHorizontalResizeCursor(manager_);
        return;
    }
    pushed_ = true;
    Invalidate();
}
void ListHeaderItem::OnButtonUp(const wxPoint&) {
    if (resizing_) {
        resizing_ = false;
        resizeTarget_ = nullptr;
        ResetCursor(manager_);
        if (manager_) {
            manager_->RequestLayout();
            manager_->Refresh();
        }
        return;
    }
    pushed_ = false;
    Invalidate();
}

// ══════════════════════════════════════════════════════════════════════════
// ListLabelElement
// ══════════════════════════════════════════════════════════════════════════

void ListLabelElement::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "textcolor" || key == "itemtextcolor") { textColor_ = ParseDWORD(val); return; }
    Control::SetAttribute(key, val);
}

void ListLabelElement::SetSelected(bool s) {
    if (selected_ == s) return;
    selected_ = s;
    Invalidate();
}

void ListLabelElement::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Control::DoPaint(dc, clipRect);
    // Background highlight
    wxColour c = ResolveListBkColor(this, hot_);
    if (c.IsOk() && c.Alpha() != 0) {
        dc.SetBrush(wxBrush(c));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(rect_);
    }
    dc.SetTextForeground(ResolveListTextColor(this));
    if (manager_) dc.SetFont(manager_->GetUIFont());
    dc.DrawLabel(Utf8ToWxString(text_), rect_, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT);
}

void ListLabelElement::OnMouseEnter(const wxPoint&)   { hot_ = true;  Invalidate(); }
void ListLabelElement::OnMouseLeave()                  { hot_ = false; Invalidate(); }
void ListLabelElement::OnButtonDown(const wxPoint&)    {}
void ListLabelElement::OnButtonUp(const wxPoint& pt)   {
    if (!rect_.Contains(pt)) return;
    if (auto* list = dynamic_cast<List*>(ownerList_))
        list->SelectItem(index_);
}
void ListLabelElement::OnButtonDblClk(const wxPoint&)  {
    if (auto* list = dynamic_cast<List*>(ownerList_)) {
        list->ActivateItem(index_);
    } else {
        FireNotify("itemactivate");
    }
}
void ListLabelElement::OnRightButtonUp(const wxPoint& pt) {
    if (!rect_.Contains(pt)) return;
    if (auto* list = dynamic_cast<List*>(ownerList_)) {
        list->ShowItemContextMenu(index_, pt.x, pt.y);
    }
}

// ══════════════════════════════════════════════════════════════════════════
// ListTextElement
// ══════════════════════════════════════════════════════════════════════════

void ListTextElement::SetColumnText(std::size_t col, const wxString& txt) {
    if (col < columns_.size() && columns_[col] == txt) return;
    if (col >= columns_.size()) columns_.resize(col + 1);
    columns_[col] = txt;
    Invalidate();
}

wxString ListTextElement::GetColumnText(std::size_t col) const {
    if (col >= columns_.size()) return {};
    return columns_[col];
}

void ListTextElement::OnButtonUp(const wxPoint& pt) {
    if (!rect_.Contains(pt)) return;
    int column = -1;
    const auto widths = ResolveColumnWidths(this, GetOwnerList(), columns_.size());
    int x = rect_.x;
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        const int width = i < widths.size() ? widths[i] : 0;
        if (pt.x >= x && pt.x < x + width) {
            column = static_cast<int>(i);
            break;
        }
        x += width;
    }
    if (column < 0 && !columns_.empty()) {
        column = static_cast<int>(columns_.size() - 1);
    }

    if (auto* list = dynamic_cast<List*>(ownerList_)) {
        list->ClickItem(index_, pt.x, pt.y, column);
    } else {
        ListLabelElement::OnButtonUp(pt);
    }
}

void ListTextElement::DoPaint(wxDC& dc, const wxRect& clipRect) {
    // Highlight
    ListLabelElement::DoPaint(dc, clipRect);
    if (!columns_.empty()) {
        const auto widths = ResolveColumnWidths(this, GetOwnerList(), columns_.size());
        int x = rect_.x;
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            const int width = i < widths.size() ? widths[i] : 0;
            wxRect colRect(x, rect_.y, width, rect_.GetHeight());
            wxRect textRect(colRect.x + 10, colRect.y,
                            std::max(0, colRect.width - 14), colRect.height);
            dc.SetTextForeground(ResolveListTextColor(this));
            if (manager_) dc.SetFont(manager_->GetUIFont());
            wxRect clippedTextRect = clipRect.Intersect(textRect);
            if (!clippedTextRect.IsEmpty()) {
                wxDCClipper clipper(dc, clippedTextRect);
                dc.DrawLabel(columns_[i], textRect,
                             wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT);
            }
            if (auto* list = dynamic_cast<List*>(GetOwnerList()); list && list->ColumnLineColor().IsOk() && i + 1 < columns_.size()) {
                dc.SetPen(wxPen(list->ColumnLineColor()));
                dc.DrawLine(colRect.GetRight(), colRect.y, colRect.GetRight(), colRect.GetBottom() + 1);
            }
            x += width;
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════
// ListContainerElement
// ══════════════════════════════════════════════════════════════════════════

void ListContainerElement::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Container::DoPaint(dc, clipRect);
    wxColour c = ResolveListContainerBkColor(this, hot_);
    if (c.IsOk() && c.Alpha() != 0) {
        dc.SetBrush(wxBrush(c));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(rect_);
    }
}

void ListContainerElement::DoLayout(const wxRect& rc) {
    rect_ = rc;
    std::size_t visibleCount = 0;
    for (const auto& child : children_) {
        if (child->IsVisible() && !child->IsFloat()) {
            ++visibleCount;
        }
    }
    const auto widths = ResolveColumnWidths(this, ownerList_, visibleCount);
    int x = rect_.x;
    std::size_t column = 0;
    for (auto& child : children_) {
        if (!child->IsVisible()) {
            child->SetRect(wxRect(x, rect_.y, 0, rect_.height));
            continue;
        }
        if (child->IsFloat()) {
            continue;
        }
        const int width = column < widths.size() ? widths[column] : 0;
        child->SetRect(wxRect(x, rect_.y, width, rect_.height));
        x += width;
        ++column;
    }
}

void ListContainerElement::SetAttribute(const std::string& key, const std::string& val) {
    Container::SetAttribute(key, val);
}

void ListContainerElement::SetSelected(bool s) {
    if (selected_ == s) return;
    selected_ = s;
    Invalidate();
}

void ListContainerElement::OnMouseEnter(const wxPoint&) { hot_ = true;  Invalidate(); }
void ListContainerElement::OnMouseLeave()               { hot_ = false; Invalidate(); }
void ListContainerElement::OnButtonDown(const wxPoint&) {}
void ListContainerElement::OnButtonUp(const wxPoint& pt) {
    if (!rect_.Contains(pt)) return;
    if (auto* list = dynamic_cast<List*>(ownerList_))
        list->SelectItem(index_);
}
void ListContainerElement::OnButtonDblClk(const wxPoint&) {
    if (auto* list = dynamic_cast<List*>(ownerList_)) {
        list->ActivateItem(index_);
    } else {
        FireNotify("itemactivate");
    }
}
void ListContainerElement::OnRightButtonUp(const wxPoint& pt) {
    if (!rect_.Contains(pt)) return;
    if (auto* list = dynamic_cast<List*>(ownerList_)) {
        list->ShowItemContextMenu(index_, pt.x, pt.y);
    }
}

// ══════════════════════════════════════════════════════════════════════════
// List
// ══════════════════════════════════════════════════════════════════════════

void List::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "stripeheight") { stripeHeight_ = std::max(0, ParseINT(val)); return; }
    if (key == "stripecolor") { stripeColor_ = ParseDWORD(val); return; }
    if (key == "header"||key=="showheader"||key=="showHeader") { showHeader_  = ParseBOOL(val); return; }
    if (key == "scrollselect")                 { scrollSelect_= ParseBOOL(val); return; }
    if (key == "hscrollcontentwidth" || key == "hScrollContentWidth" ||
        key == "contentwidth" || key == "contentWidth") {
        hScrollContentWidth_ = ParseINT(val);
        return;
    }
    if (key == "itemtextcolor")                { itemTextColor_   = ParseDWORD(val); return; }
    if (key == "itembkcolor")                  { itemBkColor_     = ParseDWORD(val); return; }
    if (key == "itemseltextcolor"||key=="itemselectedtextcolor") { itemSelTextColor_= ParseDWORD(val); return; }
    if (key == "itemselbkcolor"||key=="itemselectedbkcolor")     { itemSelBkColor_  = ParseDWORD(val); return; }
    if (key == "itemhottextcolor")             { itemHotTextColor_= ParseDWORD(val); return; }
    if (key == "itemhotbkcolor")               { itemHotBkColor_  = ParseDWORD(val); return; }
    if (key == "itemlinecolor")                { itemLineColor_   = ParseDWORD(val); return; }
    if (key == "columnlinecolor")              { columnLineColor_ = ParseDWORD(val); return; }
    if (key == "itemfont")                     { itemFontId_      = ParseINT(val);   return; }
    if (key == "itemshowhtml")                 { itemShowHtml_    = ParseBOOL(val);  return; }
    VerticalLayout::SetAttribute(key, val);
}

void List::SetHeader(ListHeader* hdr) {
    header_ = hdr;
    // Header is also a child so DoLayout accounts for it
}

wxRect List::GetItemViewportRect() const {
    wxRect inner{
        rect_.x + inset_.x, rect_.y + inset_.y,
        rect_.width - inset_.x - inset_.width,
        rect_.height - inset_.y - inset_.height
    };
    const int headerHeight =
        (header_ && showHeader_) ? header_->GetRect().GetHeight() : 0;
    const int contentWidth =
        std::max(0, inner.width - (vScrollVisible_ ? kListScrollBarWidth : 0));
    const int contentHeight =
        std::max(0, inner.height - headerHeight -
                        (hScrollVisible_ ? kListScrollBarWidth : 0));
    return wxRect(inner.x, inner.y + headerHeight, contentWidth,
                  contentHeight);
}

wxRect List::GetHeaderViewportRect() const {
    wxRect inner{
        rect_.x + inset_.x, rect_.y + inset_.y,
        rect_.width - inset_.x - inset_.width,
        rect_.height - inset_.y - inset_.height
    };
    const int headerHeight =
        (header_ && showHeader_) ? header_->GetRect().GetHeight() : 0;
    if (headerHeight <= 0) return {};
    const int contentWidth =
        std::max(0, inner.width - (vScrollVisible_ ? kListScrollBarWidth : 0));
    return wxRect(inner.x, inner.y, contentWidth, headerHeight);
}

void List::BeginUpdate() { ++updateDepth_; }

void List::EndUpdate() {
    if (!updateDepth_) return;
    if (--updateDepth_ == 0 && itemsDirty_) ItemsChanged();
}

void List::ItemsChanged() {
    itemsDirty_ = true;
    if (updateDepth_) return;
    itemsDirty_ = false;
    DoLayout(rect_);
    Invalidate();
}

void List::ClearItems() {
    virtualCount_ = -1;
    virtualFactory_ = {};
    curSel_ = -1;
    scrollPos_ = 0;
    hScrollPos_ = 0;
    vScrollRange_ = 0;
    vScrollPageSize_ = 0;
    vScrollVisible_ = false;
    vScrollDragging_ = false;
    vScrollThumbHot_ = false;
    hScrollRange_ = 0;
    hScrollPageSize_ = 0;
    hScrollVisible_ = false;
    hScrollDragging_ = false;
    hScrollThumbHot_ = false;
    if (manager_) {
        for (const auto& child : children_) {
            if (dynamic_cast<ListHeader*>(child.get()) == nullptr) {
                manager_->ForgetControlTree(child.get());
            }
        }
    }
    children_.erase(
        std::remove_if(children_.begin(), children_.end(),
                       [](const std::shared_ptr<Control>& child) {
                           return dynamic_cast<ListHeader*>(child.get()) == nullptr;
                       }),
        children_.end());
    header_ = FindHeaderChild(children_);
    ItemsChanged();
}

void List::SetVirtualItems(int count, int rowHeight, VirtualRowFactory factory, int scrollPosition) {
    BeginUpdate();
    ClearItems();
    virtualCount_ = std::clamp(count, 0, 1000000);
    virtualHeight_ = std::clamp(rowHeight, 1, 4096);
    virtualFactory_ = std::move(factory);
    scrollPos_ = std::max(0, scrollPosition);
    ItemsChanged();
    EndUpdate();
}

ListTextElement* List::AppendTextItem(const std::vector<wxString>& columns,
                                      int height) {
    auto item = std::make_shared<ListTextElement>();
    item->SetFixedHeight(height);
    for (std::size_t index = 0; index < columns.size(); ++index) {
        item->SetColumnText(index, columns[index]);
    }
    auto* raw = item.get();
    Add(item);
    ItemsChanged();
    return raw;
}

ListTextElement* List::AppendTextItemUtf8(const std::vector<std::string>& columns,
                                          int height) {
    std::vector<wxString> wxColumns;
    wxColumns.reserve(columns.size());
    for (const auto& column : columns) {
        wxColumns.push_back(Utf8ToWxString(column));
    }
    return AppendTextItem(wxColumns, height);
}

ListContainerElement* List::AppendContainerItem(
        std::shared_ptr<ListContainerElement> item, int height) {
    if (!item) return nullptr;
    item->SetFixedHeight(height);
    auto* raw = item.get();
    Add(std::move(item));
    ItemsChanged();
    return raw;
}

int List::GetItemCount() const {
    if (virtualCount_ >= 0) return virtualCount_;
    int count = 0;
    for (auto& child : children_) {
        if (dynamic_cast<ListLabelElement*>(child.get()) ||
            dynamic_cast<ListContainerElement*>(child.get()))
            ++count;
    }
    return count;
}

int List::GetItemTop(int idx) const {
    if (virtualCount_ >= 0) {
        if (idx < 0 || idx >= virtualCount_) return 0;
        const int64_t top = int64_t(GetItemViewportRect().y) + int64_t(idx) * (int64_t(virtualHeight_) + childPadding_) - scrollPos_;
        return int(std::clamp<int64_t>(top, INT_MIN, INT_MAX));
    }
    if (idx < 0) return 0;
    int i = 0;
    for (auto& child : children_) {
        if (dynamic_cast<ListLabelElement*>(child.get()) ||
            dynamic_cast<ListContainerElement*>(child.get())) {
            if (i == idx) return child->GetRect().GetTop();
            ++i;
        }
    }
    return 0;
}

void List::SelectItem(int idx, bool notify) {
    if (idx < -1 || idx >= GetItemCount()) return;
    if (idx == curSel_) return;

    // Deselect old
    if (curSel_ >= 0) {
        int i = 0;
        for (auto& child : children_) {
            if (auto* e = dynamic_cast<ListLabelElement*>(child.get())) {
                if ((virtualCount_ >= 0 ? e->GetIndex() : i) == curSel_) e->SetSelected(false);
                ++i;
            } else if (auto* c = dynamic_cast<ListContainerElement*>(child.get())) {
                if ((virtualCount_ >= 0 ? c->GetIndex() : i) == curSel_) c->SetSelected(false);
                ++i;
            }
        }
    }

    curSel_ = idx;

    // Select new
    if (curSel_ >= 0) {
        int i = 0;
        for (auto& child : children_) {
            if (auto* e = dynamic_cast<ListLabelElement*>(child.get())) {
                if ((virtualCount_ >= 0 ? e->GetIndex() : i) == curSel_) e->SetSelected(true);
                ++i;
            } else if (auto* c = dynamic_cast<ListContainerElement*>(child.get())) {
                if ((virtualCount_ >= 0 ? c->GetIndex() : i) == curSel_) c->SetSelected(true);
                ++i;
            }
        }
    }

    if (notify) FireNotify("itemselect", 0, 0, curSel_);
    Invalidate();
}

void List::ActivateItem(int idx) {
    if (idx < 0 || idx >= GetItemCount()) return;
    SelectItem(idx);
    FireNotify("itemactivate", 0, 0, idx);
}

void List::ClickItem(int idx, int x, int y, int column) {
    if (idx < 0 || idx >= GetItemCount()) return;
    SelectItem(idx);
    FireNotify("itemclick", x, y, idx, std::to_string(column));
}

void List::ShowItemContextMenu(int idx, int x, int y) {
    if (idx < 0 || idx >= GetItemCount()) return;
    SelectItem(idx);
    FireNotify("itemcontext", x, y, idx);
}

void List::UnselectAll() {
    curSel_ = -1;
    for (auto& child : children_) {
        if (auto* e = dynamic_cast<ListLabelElement*>(child.get()))
            e->SetSelected(false);
        if (auto* c = dynamic_cast<ListContainerElement*>(child.get()))
            c->SetSelected(false);
    }
    Invalidate();
}

wxRect List::GetVScrollThumbRect() const {
    if (!vScrollVisible_ || vScrollRange_ <= 0 || vScrollPageSize_ <= 0) {
        return {};
    }

    wxRect track = vScrollRect_;
    track.Deflate(kListScrollBarMargin);
    if (track.GetWidth() <= 0 || track.GetHeight() <= 0) {
        return {};
    }

    const int contentHeight = vScrollRange_ + vScrollPageSize_;
    const int thumbHeight = std::clamp(
        track.GetHeight() * vScrollPageSize_ / std::max(1, contentHeight),
        std::min(kListScrollThumbMinHeight, track.GetHeight()),
        track.GetHeight());
    const int maxOffset = std::max(0, track.GetHeight() - thumbHeight);
    const int offset = vScrollRange_ > 0
                           ? scrollPos_ * maxOffset / vScrollRange_
                           : 0;
    return wxRect(track.x, track.y + offset, track.GetWidth(), thumbHeight);
}

wxRect List::GetHScrollThumbRect() const {
    if (!hScrollVisible_ || hScrollRange_ <= 0 || hScrollPageSize_ <= 0) {
        return {};
    }

    wxRect track = hScrollRect_;
    track.Deflate(kListScrollBarMargin);
    if (track.GetWidth() <= 0 || track.GetHeight() <= 0) {
        return {};
    }

    const int contentWidth = hScrollRange_ + hScrollPageSize_;
    const int thumbWidth = std::clamp(
        track.GetWidth() * hScrollPageSize_ / std::max(1, contentWidth),
        std::min(kListScrollThumbMinHeight, track.GetWidth()),
        track.GetWidth());
    const int maxOffset = std::max(0, track.GetWidth() - thumbWidth);
    const int offset = hScrollRange_ > 0
                           ? hScrollPos_ * maxOffset / hScrollRange_
                           : 0;
    return wxRect(track.x + offset, track.y, thumbWidth, track.GetHeight());
}

void List::SetListScrollPos(int pos) {
    const int next = std::clamp(pos, 0, vScrollRange_);
    if (next == scrollPos_) return;
    scrollPos_ = next;
    DoLayout(rect_);
    if (manager_) {
        manager_->RefreshRect(rect_);
    } else {
        Invalidate();
    }
}

void List::SetListHScrollPos(int pos) {
    const int next = std::clamp(pos, 0, hScrollRange_);
    if (next == hScrollPos_) return;
    hScrollPos_ = next;
    DoLayout(rect_);
    if (manager_) {
        manager_->RefreshRect(rect_);
    } else {
        Invalidate();
    }
}

void List::PaintVScrollBar(wxDC& dc) const {
    if (vScrollVisible_) PaintScrollChrome(dc, vScrollRect_, GetVScrollThumbRect(),
        vScrollDragging_ || vScrollThumbHot_, bkColor_);
}

void List::PaintHScrollBar(wxDC& dc) const {
    if (hScrollVisible_) PaintScrollChrome(dc, hScrollRect_, GetHScrollThumbRect(),
        hScrollDragging_ || hScrollThumbHot_, bkColor_);
}

void List::OnMouseLeave() {
    if (vScrollThumbHot_ && !vScrollDragging_) {
        vScrollThumbHot_ = false;
        Invalidate();
    }
    if (hScrollThumbHot_ && !hScrollDragging_) {
        hScrollThumbHot_ = false;
        Invalidate();
    }
}

void List::OnMouseMove(const wxPoint& pt) {
    if (vScrollDragging_) {
        const wxRect thumb = GetVScrollThumbRect();
        wxRect track = vScrollRect_;
        track.Deflate(kListScrollBarMargin);
        const int maxOffset = std::max(0, track.GetHeight() - thumb.GetHeight());
        if (maxOffset > 0 && vScrollRange_ > 0) {
            const int requestedOffset =
                std::clamp(pt.y - vScrollDragOffset_ - track.y, 0, maxOffset);
            SetListScrollPos(requestedOffset * vScrollRange_ / maxOffset);
        }
        return;
    }

    if (hScrollDragging_) {
        const wxRect thumb = GetHScrollThumbRect();
        wxRect track = hScrollRect_;
        track.Deflate(kListScrollBarMargin);
        const int maxOffset = std::max(0, track.GetWidth() - thumb.GetWidth());
        if (maxOffset > 0 && hScrollRange_ > 0) {
            const int requestedOffset =
                std::clamp(pt.x - hScrollDragOffset_ - track.x, 0, maxOffset);
            SetListHScrollPos(requestedOffset * hScrollRange_ / maxOffset);
        }
        return;
    }

    const bool vHot = vScrollVisible_ && GetVScrollThumbRect().Contains(pt);
    const bool hHot = hScrollVisible_ && GetHScrollThumbRect().Contains(pt);
    if (vHot != vScrollThumbHot_ || hHot != hScrollThumbHot_) {
        vScrollThumbHot_ = vHot;
        hScrollThumbHot_ = hHot;
        Invalidate();
    }
}

void List::OnButtonDown(const wxPoint& pt) {
    if (vScrollVisible_ && vScrollRect_.Contains(pt)) {
        const wxRect thumb = GetVScrollThumbRect();
        if (thumb.Contains(pt)) {
            vScrollDragging_ = true;
            vScrollThumbHot_ = true;
            vScrollDragOffset_ = pt.y - thumb.y;
            Invalidate();
            return;
        }

        SetListScrollPos(scrollPos_ + (pt.y < thumb.y ? -vScrollPageSize_
                                                      : vScrollPageSize_));
        return;
    }

    if (hScrollVisible_ && hScrollRect_.Contains(pt)) {
        const wxRect thumb = GetHScrollThumbRect();
        if (thumb.Contains(pt)) {
            hScrollDragging_ = true;
            hScrollThumbHot_ = true;
            hScrollDragOffset_ = pt.x - thumb.x;
            Invalidate();
            return;
        }

        SetListHScrollPos(hScrollPos_ + (pt.x < thumb.x ? -hScrollPageSize_
                                                        : hScrollPageSize_));
    }
}

void List::OnButtonUp(const wxPoint&) {
    if (!vScrollDragging_ && !hScrollDragging_) return;
    vScrollDragging_ = false;
    hScrollDragging_ = false;
    Invalidate();
}

bool List::OnMouseWheel(int rotation, int delta) {
    if (!vScrollVisible_ || vScrollRange_ <= 0) return false;

    const int wheelDelta = delta == 0 ? 120 : std::abs(delta);
    int notches = rotation / wheelDelta;
    if (notches == 0) {
        notches = rotation > 0 ? 1 : -1;
    }
    SetListScrollPos(scrollPos_ - notches * (28 + childPadding_) *
                                      kListWheelRows);
    return true;
}

void List::OnSetFocus() {
    Container::OnSetFocus();
    FireNotify("setfocus");
}

void List::OnKillFocus() {
    Container::OnKillFocus();
    FireNotify("killfocus");
}

void List::ScrollToTop() {
    SetListScrollPos(0);
}

void List::RestoreListScrollPos(int pos) {
    SetListScrollPos(pos);
}

void List::DoLayout(const wxRect& rc) {
    rect_ = rc;
    header_ = FindHeaderChild(children_);

    wxRect inner{
        rc.x + inset_.x, rc.y + inset_.y,
        rc.width - inset_.x - inset_.width,
        rc.height - inset_.y - inset_.height
    };

    const int headerHeight =
        (header_ && showHeader_)
            ? (header_->GetFixedSize().y > 0 ? header_->GetFixedSize().y : 22)
            : 0;
    const int viewportY = inner.y + headerHeight;
    const int baseViewportHeight = std::max(0, inner.height - headerHeight);

    int contentHeight = 0;
    int visibleItemCount = 0;
    for (auto& child : children_) {
        if (child.get() == static_cast<Control*>(header_)) continue;
        if (!child->IsVisible()) continue;
        if (visibleItemCount > 0) contentHeight += childPadding_;
        const wxSize fixed = child->GetFixedSize();
        contentHeight += (fixed.y > 0) ? fixed.y : 24;
        ++visibleItemCount;
    }
    if (virtualCount_ >= 0)
        contentHeight = int(std::min<int64_t>(INT_MAX / 2, int64_t(virtualCount_) * (virtualHeight_ + childPadding_) - (virtualCount_ ? childPadding_ : 0)));

    bool vVisible = false;
    bool hVisible = false;
    int viewportWidth = std::max(0, inner.width);
    int viewportHeight = baseViewportHeight;
    int layoutContentWidth = ResolveListContentWidth(
        header_, hScrollContentWidth_, viewportWidth);

    for (int pass = 0; pass < 3; ++pass) {
        vVisible = vScrollBar_ && contentHeight > viewportHeight &&
                   inner.width > kListScrollBarWidth && viewportHeight > 0;
        viewportWidth =
            std::max(0, inner.width - (vVisible ? kListScrollBarWidth : 0));
        layoutContentWidth = ResolveListContentWidth(
            header_, hScrollContentWidth_, viewportWidth);
        hVisible = hScrollBar_ && layoutContentWidth > viewportWidth &&
                   viewportWidth > 0 &&
                   baseViewportHeight > kListScrollBarWidth;
        viewportHeight = std::max(
            0, baseViewportHeight - (hVisible ? kListScrollBarWidth : 0));
    }

    vScrollRange_ = std::max(0, contentHeight - viewportHeight);
    vScrollPageSize_ = std::max(1, viewportHeight);
    scrollPos_ = std::clamp(scrollPos_, 0, vScrollRange_);
    vScrollVisible_ = vVisible && vScrollRange_ > 0;
    vScrollRect_ = vScrollVisible_
                       ? wxRect(inner.GetRight() - kListScrollBarWidth + 1,
                                viewportY, kListScrollBarWidth, viewportHeight)
                       : wxRect{};

    viewportWidth =
        std::max(0, inner.width - (vScrollVisible_ ? kListScrollBarWidth : 0));
    layoutContentWidth = ResolveListContentWidth(
        header_, hScrollContentWidth_, viewportWidth);
    hScrollRange_ = std::max(0, layoutContentWidth - viewportWidth);
    hScrollPageSize_ = std::max(1, viewportWidth);
    hScrollPos_ = std::clamp(hScrollPos_, 0, hScrollRange_);
    hScrollVisible_ = hVisible && hScrollRange_ > 0;
    hScrollRect_ = hScrollVisible_
                       ? wxRect(inner.x,
                                inner.GetBottom() - kListScrollBarWidth + 1,
                                viewportWidth, kListScrollBarWidth)
                       : wxRect{};
    if (!hScrollVisible_) {
        hScrollPos_ = 0;
    }

    if (header_ && showHeader_) {
        header_->SetRect(wxRect(inner.x - hScrollPos_, inner.y,
                                layoutContentWidth, headerHeight));
    }

    if (virtualCount_ >= 0) {
        const int pitch = virtualHeight_ + childPadding_;
        const int first = std::max(0, scrollPos_ / std::max(1, pitch) - 1);
        const int last = std::min(virtualCount_, (scrollPos_ + viewportHeight + pitch - 1) / std::max(1, pitch) + 1);
        for (auto it = children_.begin(); it != children_.end();) {
            auto* row = dynamic_cast<ListContainerElement*>(it->get());
            if (row && (row->GetIndex() < first || row->GetIndex() >= last)) {
                if (manager_) manager_->ForgetControlTree(row);
                it = children_.erase(it);
            } else ++it;
        }
        for (int index = first; index < last; ++index) {
            ListContainerElement* found = nullptr;
            for (const auto& child : children_) {
                auto* row = dynamic_cast<ListContainerElement*>(child.get());
                if (row && row->GetIndex() == index) { found = row; break; }
            }
            if (!found && virtualFactory_) {
                auto row = virtualFactory_(index);
                if (!row) continue;
                found = row.get(); row->SetOwnerList(this); row->SetIndex(index); row->SetFixedHeight(virtualHeight_);
                Add(std::move(row));
            }
            if (found) {
                found->SetSelected(index == curSel_);
                found->SetRect(wxRect(inner.x - hScrollPos_, viewportY + index * pitch - scrollPos_, layoutContentWidth, virtualHeight_));
            }
        }
        return;
    }

    int y = viewportY - scrollPos_;

    // Items
    int itemIdx = 0;
    for (auto& child : children_) {
        if (child.get() == static_cast<Control*>(header_)) continue;
        if (!child->IsVisible()) continue;

        // Propagate owner / index
        if (auto* e = dynamic_cast<ListLabelElement*>(child.get())) {
            e->SetOwnerList(this); e->SetIndex(itemIdx);
        }
        if (auto* c = dynamic_cast<ListContainerElement*>(child.get())) {
            c->SetOwnerList(this); c->SetIndex(itemIdx);
        }

        const wxSize fixed = child->GetFixedSize();
        const int h = (fixed.y > 0) ? fixed.y : 24;
        child->SetRect(wxRect(inner.x - hScrollPos_, y,
                              layoutContentWidth, h));

        y += h + childPadding_;
        ++itemIdx;
    }
}

void List::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Container::DoPaint(dc, clipRect);
    if (stripeHeight_ > 0 && stripeColor_.IsOk()) {
        const auto area = GetItemViewportRect();
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(stripeColor_));
        wxDCClipper clipper(dc, area.Intersect(clipRect));
        // Occupied stripes follow row geometry, including wrapped rows and
        // partial scrolling. A viewport-anchored grid drifts across the text.
        int nextIndex = 0, tail = area.y - scrollPos_;
        for (const auto& child : children_) {
            if (child.get() == header_ || !child->IsVisible()) continue;
            int index = nextIndex++;
            if (virtualCount_ >= 0) {
                if (const auto* row = dynamic_cast<const ListContainerElement*>(child.get()))
                    index = row->GetIndex();
            }
            const auto& row = child->GetRect();
            if (index % 2 == 0 && row.Intersects(area))
                dc.DrawRectangle(area.x, row.y, area.width, row.height);
            tail = std::max(tail, row.GetBottom() + 1 + childPadding_);
        }
        if (virtualCount_ >= 0) {
            nextIndex = virtualCount_;
            tail = area.y - scrollPos_ + virtualCount_ * (virtualHeight_ + childPadding_);
        }
        // Keep the familiar empty-table stripes below the last actual row.
        for (int y = tail; y < area.GetBottom() + 1; y += stripeHeight_, ++nextIndex)
            if (nextIndex % 2 == 0)
                dc.DrawRectangle(area.x, y, area.width, stripeHeight_);
    }
    if (columnLineColor_.IsOk() && header_ && showHeader_) {
        const auto area = GetItemViewportRect();
        wxDCClipper clipper(dc, area.Intersect(clipRect));
        dc.SetPen(wxPen(columnLineColor_));
        for (const auto& cell : header_->GetChildren()) {
            const auto x = cell->GetRect().GetRight();
            if (cell->IsVisible() && x < area.GetRight()) dc.DrawLine(x, area.y, x, area.GetBottom() + 1);
        }
    }
    // Draw separator lines
    if (itemLineColor_.IsOk()) {
        dc.SetPen(wxPen(itemLineColor_));
        for (auto& child : children_) {
            if (!child->IsVisible()) continue;
            const wxRect& r = child->GetRect();
            dc.DrawLine(r.GetLeft(), r.GetBottom(),
                        r.GetRight(), r.GetBottom());
        }
    }
    PaintVScrollBar(dc);
    PaintHScrollBar(dc);
    if (vScrollVisible_ && hScrollVisible_) {
        wxRect corner(vScrollRect_.x, hScrollRect_.y,
                      vScrollRect_.GetWidth(), hScrollRect_.GetHeight());
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(ChromeFor(bkColor_).track));
        dc.DrawRectangle(corner);
    }
}

} // namespace wxui
