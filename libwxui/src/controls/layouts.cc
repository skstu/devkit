/// libwxui — layout containers implementation

#include <libwxui.hpp>
#include <libwxui/appearance.hpp>

#include <algorithm>
#include <cstdlib>

namespace wxui {

namespace {

constexpr int kMinSplitterThickness = 4;
constexpr int kDefaultMinVerticalResize = 80;
constexpr int kDefaultMinHorizontalResize = 120;

int SplitterThickness(int sep) {
    return std::max(kMinSplitterThickness, std::abs(sep));
}

int ClampResizeSize(int value, int minimum, int maximum) {
    if (maximum < minimum) maximum = minimum;
    return std::min(std::max(value, minimum), maximum);
}

void SetResizeCursor(UIManager* manager, wxStockCursor cursor) {
    if (!manager) return;
    manager->SetCursor(wxCursor(cursor));
}

void ResetCursor(UIManager* manager) {
    if (!manager) return;
    manager->SetCursor(wxCursor(wxCURSOR_ARROW));
}

int MaxVerticalTargetSize(const VerticalLayout* layout, const Control* target) {
    if (!layout || !target) return 0;

    const wxRect rc = layout->GetRect();
    const wxRect inset = layout->GetInset();
    const int innerHeight = std::max(0, rc.height - inset.y - inset.height);

    int otherHeight = 0;
    int visibleCount = 0;
    for (const auto& child : layout->GetChildren()) {
        if (!child->IsVisible()) continue;
        ++visibleCount;
        if (child.get() == target) continue;

        const wxSize fixed = child->GetFixedSize();
        const wxSize minimum = child->GetMinSize();
        otherHeight += fixed.y > 0 ? fixed.y : minimum.y;
    }

    const int gaps = visibleCount > 1
        ? layout->GetChildPadding() * (visibleCount - 1)
        : 0;
    return std::max(0, innerHeight - otherHeight - gaps);
}

int MaxHorizontalTargetSize(const HorizontalLayout* layout, const Control* target) {
    if (!layout || !target) return 0;

    const wxRect rc = layout->GetRect();
    const wxRect inset = layout->GetInset();
    const int innerWidth = std::max(0, rc.width - inset.x - inset.width);

    int otherWidth = 0;
    int visibleCount = 0;
    for (const auto& child : layout->GetChildren()) {
        if (!child->IsVisible()) continue;
        ++visibleCount;
        if (child.get() == target) continue;

        const wxSize fixed = child->GetFixedSize();
        const wxSize minimum = child->GetMinSize();
        otherWidth += fixed.x > 0 ? fixed.x : minimum.x;
    }

    const int gaps = visibleCount > 1
        ? layout->GetChildPadding() * (visibleCount - 1)
        : 0;
    return std::max(0, innerWidth - otherWidth - gaps);
}

} // namespace

// ════════════════════════════════════════════════════════════════════════
// VerticalLayout
// ════════════════════════════════════════════════════════════════════════

void VerticalLayout::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "sepheight" || key == "sepHeight") { sepHeight_ = ParseINT(val); return; }
    if (key == "sepimm"    || key == "sepImm")    { sepImm_    = ParseBOOL(val); return; }
    Container::SetAttribute(key, val);
}

Control* VerticalLayout::SplitterTarget() const {
    if (sepHeight_ == 0) return nullptr;

    std::vector<Control*> visible;
    for (const auto& child : children_) {
        if (child->IsVisible()) visible.push_back(child.get());
    }
    if (visible.size() < 2) return nullptr;

    return sepHeight_ > 0 ? visible.front() : visible.back();
}

wxRect VerticalLayout::SplitterRect() const {
    Control* target = SplitterTarget();
    if (!target) return {};

    const wxRect targetRect = target->GetRect();
    const int thickness = SplitterThickness(sepHeight_);
    if (sepHeight_ > 0) {
        return wxRect(rect_.x, targetRect.GetBottom() + 1,
                      rect_.GetWidth(), thickness);
    }
    return wxRect(rect_.x, targetRect.GetTop() - thickness,
                  rect_.GetWidth(), thickness);
}

void VerticalLayout::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Container::DoPaint(dc, clipRect);

    const wxRect splitter = SplitterRect();
    if (splitter.IsEmpty() || !clipRect.Intersects(splitter)) return;

    PaintDivider(dc, splitter, false, resizing_,
        manager_ && manager_->GetRoot() ? manager_->GetRoot()->GetBkColor() : bkColor_);
}

void VerticalLayout::DoLayout(const wxRect& rc) {
    rect_ = rc;
    wxRect inner{
        rc.x + inset_.x,
        rc.y + inset_.y,
        std::max(0, rc.width - inset_.x - inset_.width),
        std::max(0, rc.height - inset_.y - inset_.height)
    };

    int y = inner.y;

    // Count auto-height children (fixedSize_.y == 0)
    int fixed = 0, autoWeight = 0, visibleCount = 0;
    for (auto& c : children_) {
        if (!c->IsVisible()) continue;
        ++visibleCount;
        const wxSize fs = c->GetFixedSize();
        const wxRect margin = c->GetPadding();
        fixed += margin.y + margin.height;
        if (fs.y > 0) fixed += fs.y;
        else          autoWeight += c->GetLayoutWeight();
    }
    // Include gaps between children
    if (visibleCount > 1) fixed += childPadding_ * (visibleCount - 1);
    const int available = std::max(0, inner.GetHeight() - fixed);
    int usedWeight = 0;

    bool first = true;
    for (auto& c : children_) {
        if (!c->IsVisible()) { c->SetRect(wxRect(inner.x, y, inner.GetWidth(), 0)); continue; }
        if (!first) y += childPadding_;
        first = false;
        const wxSize fs = c->GetFixedSize();
        const wxRect margin = c->GetPadding();
        const int h = fs.y > 0 ? fs.y :
            int(int64_t(available) * (usedWeight + c->GetLayoutWeight()) / std::max(1, autoWeight)) -
            int(int64_t(available) * usedWeight / std::max(1, autoWeight));
        if (fs.y <= 0) usedWeight += c->GetLayoutWeight();
        const int w = fs.x > 0 ? fs.x : std::max(0, inner.GetWidth() - margin.x - margin.width);
        c->SetRect(wxRect(inner.x + margin.x, y + margin.y, w, h));
        y += h + margin.y + margin.height;
    }
}

void VerticalLayout::OnButtonDown(const wxPoint& pt) {
    const wxRect splitter = SplitterRect();
    Control* target = SplitterTarget();
    if (!target || splitter.IsEmpty() || !splitter.Contains(pt)) return;

    resizing_ = true;
    resizeTarget_ = target;
    resizeStartY_ = pt.y;
    resizeStartHeight_ = target->GetRect().GetHeight();
    SetResizeCursor(manager_, wxCURSOR_SIZENS);
    if (manager_) manager_->RefreshRect(SplitterRect(), false);
}

void VerticalLayout::OnMouseMove(const wxPoint& pt) {
    if (resizing_ && resizeTarget_) {
        const int delta = pt.y - resizeStartY_;
        const int requested = resizeStartHeight_ +
            (sepHeight_ > 0 ? delta : -delta);
        const int minimum = std::max(kDefaultMinVerticalResize,
                                     resizeTarget_->GetMinSize().y);
        int maximum = MaxVerticalTargetSize(this, resizeTarget_);
        const int targetMax = resizeTarget_->GetMaxSize().y;
        if (targetMax > 0) maximum = std::min(maximum, targetMax);

        const int height = ClampResizeSize(requested, minimum, maximum);
        if (resizeTarget_->GetFixedSize().y == height) return;
        resizeTarget_->SetFixedHeight(height);
        SetResizeCursor(manager_, wxCURSOR_SIZENS);
        // Only this container's children change. Relaying out the root also
        // touched every unrelated native editor on each mouse movement.
        SetRect(rect_);
        Invalidate();
        return;
    }

    if (SplitterRect().Contains(pt)) {
        SetResizeCursor(manager_, wxCURSOR_SIZENS);
    } else {
        ResetCursor(manager_);
    }
}

void VerticalLayout::OnMouseLeave() {
    if (!resizing_) ResetCursor(manager_);
}

void VerticalLayout::OnButtonUp(const wxPoint&) {
    if (!resizing_) return;

    resizing_ = false;
    resizeTarget_ = nullptr;
    ResetCursor(manager_);
    if (manager_) manager_->RefreshRect(SplitterRect(), false);
}

// ════════════════════════════════════════════════════════════════════════
// HorizontalLayout
// ════════════════════════════════════════════════════════════════════════

void HorizontalLayout::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "sepwidth" || key == "sepWidth") { sepWidth_ = ParseINT(val); return; }
    if (key == "sepimm"   || key == "sepImm")   { sepImm_   = ParseBOOL(val); return; }
    Container::SetAttribute(key, val);
}

Control* HorizontalLayout::SplitterTarget() const {
    if (sepWidth_ == 0) return nullptr;

    std::vector<Control*> visible;
    for (const auto& child : children_) {
        if (child->IsVisible()) visible.push_back(child.get());
    }
    if (visible.size() < 2) return nullptr;

    return sepWidth_ > 0 ? visible.front() : visible.back();
}

wxRect HorizontalLayout::SplitterRect() const {
    Control* target = SplitterTarget();
    if (!target) return {};

    const wxRect targetRect = target->GetRect();
    const int thickness = SplitterThickness(sepWidth_);
    if (sepWidth_ > 0) {
        return wxRect(targetRect.GetRight() + 1, rect_.y,
                      thickness, rect_.GetHeight());
    }
    return wxRect(targetRect.GetLeft() - thickness, rect_.y,
                  thickness, rect_.GetHeight());
}

void HorizontalLayout::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Container::DoPaint(dc, clipRect);

    const wxRect splitter = SplitterRect();
    if (splitter.IsEmpty() || !clipRect.Intersects(splitter)) return;

    PaintDivider(dc, splitter, true, resizing_,
        manager_ && manager_->GetRoot() ? manager_->GetRoot()->GetBkColor() : bkColor_);
}

void HorizontalLayout::DoLayout(const wxRect& rc) {
    rect_ = rc;
    wxRect inner{
        rc.x + inset_.x,
        rc.y + inset_.y,
        std::max(0, rc.width - inset_.x - inset_.width),
        std::max(0, rc.height - inset_.y - inset_.height)
    };

    int x = inner.x;

    int fixed = 0, autoWeight = 0, visibleCount = 0;
    for (auto& c : children_) {
        if (!c->IsVisible()) continue;
        ++visibleCount;
        const wxSize fs = c->GetFixedSize();
        const wxRect margin = c->GetPadding();
        fixed += margin.x + margin.width;
        if (fs.x > 0) fixed += fs.x;
        else          autoWeight += c->GetLayoutWeight();
    }
    // Include gaps between children
    if (visibleCount > 1) fixed += childPadding_ * (visibleCount - 1);
    const int available = std::max(0, inner.GetWidth() - fixed);
    int usedWeight = 0;

    bool first = true;
    for (auto& c : children_) {
        if (!c->IsVisible()) { c->SetRect(wxRect(x, inner.y, 0, inner.GetHeight())); continue; }
        if (!first) x += childPadding_;
        first = false;
        const wxSize fs = c->GetFixedSize();
        const wxRect margin = c->GetPadding();
        const int w = fs.x > 0 ? fs.x :
            int(int64_t(available) * (usedWeight + c->GetLayoutWeight()) / std::max(1, autoWeight)) -
            int(int64_t(available) * usedWeight / std::max(1, autoWeight));
        if (fs.x <= 0) usedWeight += c->GetLayoutWeight();
        const int h = fs.y > 0 ? fs.y : std::max(0, inner.GetHeight() - margin.y - margin.height);
        c->SetRect(wxRect(x + margin.x, inner.y + margin.y, w, h));
        x += w + margin.x + margin.width;
    }
}

void HorizontalLayout::OnButtonDown(const wxPoint& pt) {
    const wxRect splitter = SplitterRect();
    Control* target = SplitterTarget();
    if (!target || splitter.IsEmpty() || !splitter.Contains(pt)) return;

    resizing_ = true;
    resizeTarget_ = target;
    resizeStartX_ = pt.x;
    resizeStartWidth_ = target->GetRect().GetWidth();
    SetResizeCursor(manager_, wxCURSOR_SIZEWE);
    if (manager_) manager_->RefreshRect(SplitterRect(), false);
}

void HorizontalLayout::OnMouseMove(const wxPoint& pt) {
    if (resizing_ && resizeTarget_) {
        const int delta = pt.x - resizeStartX_;
        const int requested = resizeStartWidth_ +
            (sepWidth_ > 0 ? delta : -delta);
        const int minimum = std::max(kDefaultMinHorizontalResize,
                                     resizeTarget_->GetMinSize().x);
        int maximum = MaxHorizontalTargetSize(this, resizeTarget_);
        const int targetMax = resizeTarget_->GetMaxSize().x;
        if (targetMax > 0) maximum = std::min(maximum, targetMax);

        const int width = ClampResizeSize(requested, minimum, maximum);
        if (resizeTarget_->GetFixedSize().x == width) return;
        resizeTarget_->SetFixedWidth(width);
        SetResizeCursor(manager_, wxCURSOR_SIZEWE);
        SetRect(rect_);
        Invalidate();
        return;
    }

    if (SplitterRect().Contains(pt)) {
        SetResizeCursor(manager_, wxCURSOR_SIZEWE);
    } else {
        ResetCursor(manager_);
    }
}

void HorizontalLayout::OnMouseLeave() {
    if (!resizing_) ResetCursor(manager_);
}

void HorizontalLayout::OnButtonUp(const wxPoint&) {
    if (!resizing_) return;

    resizing_ = false;
    resizeTarget_ = nullptr;
    ResetCursor(manager_);
    if (manager_) manager_->RefreshRect(SplitterRect(), false);
}

// ════════════════════════════════════════════════════════════════════════
// TileLayout
// ════════════════════════════════════════════════════════════════════════

void TileLayout::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "columns")  { columns_  = ParseINT(val, 1); return; }
    if (key == "itemsize") { itemSize_ = ParseSIZE(val);   return; }
    Container::SetAttribute(key, val);
}

void TileLayout::DoLayout(const wxRect& rc) {
    rect_ = rc;
    wxRect inner{
        rc.x + inset_.x,
        rc.y + inset_.y,
        std::max(0, rc.width - inset_.x - inset_.width),
        std::max(0, rc.height - inset_.y - inset_.height)
    };

    const int cols = std::max(1, columns_);
    const int iw   = itemSize_.x > 0 ? itemSize_.x : inner.GetWidth() / cols;
    const int ih   = itemSize_.y > 0 ? itemSize_.y : iw;

    int col = 0, row = 0;
    for (auto& c : children_) {
        if (!c->IsVisible()) continue;
        const int x = inner.x + col * iw;
        const int y = inner.y + row * ih;
        c->SetRect(wxRect(x, y, iw, ih));
        if (++col >= cols) { col = 0; ++row; }
    }
}

// ════════════════════════════════════════════════════════════════════════
// TabLayout
// ════════════════════════════════════════════════════════════════════════

void TabLayout::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "selectedid" || key == "selectedId") {
        SelectItem(val);
        return;
    }
    Container::SetAttribute(key, val);
}

void TabLayout::SelectItem(const std::string& name) {
    selectedId_ = name;
    ApplySelection();
    Invalidate();
    if (manager_) {
        manager_->RequestLayout();
    }
}

void TabLayout::ApplySelection() {
    const int selectedIndex = ParseINT(selectedId_, -1);
    int index = 0;
    for (auto& c : children_) {
        const bool byName = c->GetName() == selectedId_;
        const bool byIndex = selectedIndex >= 0 && index == selectedIndex;
        c->SetVisible(byName || byIndex || selectedId_.empty());
        ++index;
    }
}

void TabLayout::DoLayout(const wxRect& rc) {
    rect_ = rc;
    ApplySelection();
    for (auto& c : children_) {
        c->SetRect(rc);
    }
}

void TabLayout::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Container::DoPaint(dc, clipRect);
}

// ════════════════════════════════════════════════════════════════════════
// ChildLayout
// ════════════════════════════════════════════════════════════════════════

void ChildLayout::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "xmlfile" || key == "xmlFile") { xmlFile_ = val; return; }
    Container::SetAttribute(key, val);
}

void ChildLayout::OnManagerSet() {
    if (loaded_ || xmlFile_.empty() || !manager_) return;
    loaded_ = true;
    // Delegate child-XML loading to UIManager
    manager_->LoadChildLayout(this, xmlFile_);
}

} // namespace wxui
