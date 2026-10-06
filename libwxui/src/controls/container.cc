#include <libwxui.hpp>

#include <algorithm>
#include <wx/log.h>

namespace wxui {

// ── SetAttribute ─────────────────────────────────────────────────────────
void Container::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "inset")          { inset_      = ParseRECT(val);             return; }
    if (key == "mousechild" ||
        key == "mouseChild")     { mouseChild_ = ParseBOOL(val, true);       return; }
    if (key == "childpadding" ||
        key == "childPadding")   { childPadding_ = ParseINT(val);            return; }
    if (key == "vscrollbar" ||
        key == "vScrollBar")     { vScrollBar_ = ParseBOOL(val);             return; }
    if (key == "hscrollbar" ||
        key == "hScrollBar")     { hScrollBar_ = ParseBOOL(val);             return; }
    Control::SetAttribute(key, val);
}

// ── Child management ──────────────────────────────────────────────────────
void Container::Add(std::shared_ptr<Control> child) {
    if (!child) return;
    child->SetParent(this);
    child->SetManager(manager_);
    children_.push_back(std::move(child));
}

void Container::InsertAt(std::size_t idx, std::shared_ptr<Control> child) {
    if (!child) return;
    child->SetParent(this);
    child->SetManager(manager_);
    idx = std::min(idx, children_.size());
    children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(idx),
                     std::move(child));
}

void Container::Remove(Control* child) {
    if (manager_) {
        for (const auto& current : children_) {
            if (current.get() == child) {
                manager_->ForgetControlTree(child);
                break;
            }
        }
    }
    auto it = std::remove_if(children_.begin(), children_.end(),
        [child](const std::shared_ptr<Control>& p){ return p.get() == child; });
    children_.erase(it, children_.end());
}

void Container::RemoveAll() {
    if (manager_) {
        for (const auto& child : children_) {
            manager_->ForgetControlTree(child.get());
        }
    }
    children_.clear();
}


// ── Manager propagation ───────────────────────────────────────────────────
void Container::SetManager(UIManager* mgr) {
    Control::SetManager(mgr);
    for (auto& c : children_) c->SetManager(mgr);
}

void Container::PropagateManager(UIManager* m) {
    for (auto& c : children_) c->SetManager(m);
}

void Container::OnManagerSet() {
    PropagateManager(manager_);
}

// ── FindControl ───────────────────────────────────────────────────────────
Control* Container::FindControl(const std::string& name) {
    if (name_ == name) return this;
    for (auto& child : children_) {
        Control* found = child->FindControl(name);
        if (found) return found;
    }
    return nullptr;
}

Control* Container::FindControlByPoint(const wxPoint& pt) {
    if (!rect_.Contains(pt)) return nullptr;
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        auto& child = *it;
        if (!child->IsVisible() || !child->IsMouseEnabled()) continue;
        if (!child->GetRect().Contains(pt)) continue;
        if (auto* sub = dynamic_cast<Container*>(child.get())) {
            Control* inner = sub->FindControlByPoint(pt);
            if (inner) return inner;
        }
        return child.get();
    }
    return this;
}

// ── SetRect / DoLayout ────────────────────────────────────────────────────
void Container::SetRect(const wxRect& rc) {
    Control::SetRect(rc);
    DoLayout(rc);
}

void Container::DoLayout(const wxRect& rc) {
    // Base implementation: position each non-float child respecting inset
    wxRect inner{
        rc.x + inset_.x,
        rc.y + inset_.y,
        rc.width  - inset_.x - inset_.width,
        rc.height - inset_.y - inset_.height
    };

    for (auto& child : children_) {
        if (child->IsFloat()) continue;
        const wxSize fixed = child->GetFixedSize();

        int w = (fixed.x > 0) ? fixed.x : inner.width;
        int h = (fixed.y > 0) ? fixed.y : inner.height;
        w = std::max(w, child->GetMinSize().x);
        h = std::max(h, child->GetMinSize().y);
        if (child->GetMaxSize().x > 0) w = std::min(w, child->GetMaxSize().x);
        if (child->GetMaxSize().y > 0) h = std::min(h, child->GetMaxSize().y);

        child->SetRect(wxRect(inner.x, inner.y, w, h));
    }
}

// ── DoPaint ───────────────────────────────────────────────────────────────
void Container::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Control::DoPaint(dc, clipRect);
    // Children painted by UIManager::PaintTree to control stacking / clipping
}

} // namespace wxui
