#include <libwxui.hpp>

#include <wx/log.h>
#include <algorithm>

namespace wxui {

// ══════════════════════════════════════════════════════════════════════════
// TreeNode
// ══════════════════════════════════════════════════════════════════════════

void TreeNode::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "expanded")    { SetExpanded(ParseBOOL(val, true));   return; }
    if (key == "checked")     { SetChecked(ParseBOOL(val, false));   return; }
    if (key == "level")       { level_ = ParseINT(val);              return; }
    if (key == "itemheight")  { itemHeight_ = ParseINT(val, 24);     return; }
    if (key == "textcolor")        { textColor_      = ParseDWORD(val); return; }
    if (key == "seltextcolor")     { selTextColor_   = ParseDWORD(val); return; }
    if (key == "hottextcolor")     { hotTextColor_   = ParseDWORD(val); return; }
    if (key == "selhotextcolor")   { selHotTextColor_= ParseDWORD(val); return; }
    ListContainerElement::SetAttribute(key, val);
}

void TreeNode::SetExpanded(bool e) {
    if (expanded_ == e) return;
    expanded_ = e;
    // Notify owner tree to rebuild flat list
    if (auto* tv = dynamic_cast<TreeView*>(ownerList_)) {
        tv->DoLayout(tv->GetRect());
        tv->Invalidate();
        if (auto* manager = tv->GetManager()) {
            manager->RefreshRect(tv->GetRect());
        }
    } else {
        Invalidate();
    }
}

void TreeNode::AddTreeChild(std::shared_ptr<TreeNode> child) {
    child->SetLevel(level_ + 1);
    treeChildren_.push_back(std::move(child));
    if (auto* tv = dynamic_cast<TreeView*>(ownerList_))
        tv->DoLayout(tv->GetRect());
}

void TreeNode::RemoveTreeChildren() {
    if (manager_) {
        for (const auto& child : treeChildren_) {
            manager_->ForgetControlTree(child.get());
        }
    }
    treeChildren_.clear();
}

wxRect TreeNode::FolderBtnRect() const {
    const int sz = 12;
    const int indent = level_ * 16 + 4;
    const int cy = rect_.y + rect_.GetHeight() / 2 - sz / 2;
    return wxRect(rect_.x + indent, cy, sz, sz);
}

void TreeNode::DoPaint(wxDC& dc, const wxRect& clipRect) {
    // Background
    Control::DoPaint(dc, clipRect);
    if (selected_ || hot_) {
        wxColour c;
        if (auto* list = dynamic_cast<List*>(ownerList_)) {
            c = selected_ ? list->itemSelBkColor_ : list->itemHotBkColor_;
        }
        if (!c.IsOk()) {
            c = selected_ ? wxColour(0xBA, 0xE4, 0xFF)
                          : wxColour(0xD8, 0xF0, 0xFF);
        }
        dc.SetBrush(wxBrush(c));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(rect_);
    }

    // Folder toggle button
    if (!treeChildren_.empty()) {
        wxRect btn = FolderBtnRect();
        dc.SetPen(*wxGREY_PEN);
        dc.SetBrush(*wxWHITE_BRUSH);
        dc.DrawRectangle(btn);
        // "+" or "-"
        dc.DrawLine(btn.x + 2,           btn.y + btn.GetHeight()/2,
                    btn.GetRight() - 1,  btn.y + btn.GetHeight()/2);
        if (!expanded_)
            dc.DrawLine(btn.x + btn.GetWidth()/2, btn.y + 2,
                        btn.x + btn.GetWidth()/2, btn.GetBottom() - 1);
    }

    // Text with indent
    const int indent = level_ * 16 + 20;
    wxRect textRect(rect_.x + indent, rect_.y,
                    rect_.GetWidth() - indent, rect_.GetHeight());
    wxColour col = selected_ ? selTextColor_ : textColor_;
    if (!col.IsOk()) {
        if (auto* list = dynamic_cast<List*>(ownerList_)) {
            col = selected_ ? list->itemSelTextColor_ : list->itemTextColor_;
        }
    }
    if (!col.IsOk()) col = *wxBLACK;
    dc.SetTextForeground(col);
    if (manager_) dc.SetFont(manager_->GetUIFont());
    dc.DrawLabel(Utf8ToWxString(text_), textRect, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT);
}

void TreeNode::OnButtonUp(const wxPoint& pt) {
    if (!rect_.Contains(pt)) return;

    ListContainerElement::OnButtonUp(pt);
    if (!treeChildren_.empty()) {
        SetExpanded(!expanded_);
    }
}

// ══════════════════════════════════════════════════════════════════════════
// TreeView
// ══════════════════════════════════════════════════════════════════════════

void TreeView::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "multipleitem")     { multipleItem_     = ParseBOOL(val);   return; }
    if (key == "itemcheckimgsize") { itemCheckImgSize_ = ParseSIZE(val);   return; }
    if (key == "itemiconimgsize")  { itemIconImgSize_  = ParseSIZE(val);   return; }
    if (key == "visiblefolderbtn") { visibleFolderBtn_ = ParseBOOL(val);   return; }
    if (key == "visiblecheckbtn")  { visibleCheckBtn_  = ParseBOOL(val);   return; }
    if (key == "itemminwidth")     { itemMinWidth_     = ParseINT(val);    return; }
    if (key == "selitemtextcolor") { selItemTextColor_ = ParseDWORD(val);  return; }
    if (key == "selitemhottextcolor"){selItemHotTextColor_=ParseDWORD(val);return; }
    List::SetAttribute(key, val);
}

void TreeView::AddRoot(std::shared_ptr<TreeNode> node) {
    node->SetLevel(0);
    roots_.push_back(node);
    BuildFlatList();
    DoLayout(rect_);
    Invalidate();
}

void TreeView::RemoveAllRoots() {
    UnselectAll();
    if (manager_) {
        for (const auto& root : roots_) {
            manager_->ForgetControlTree(root.get());
        }
    }
    roots_.clear();
    flatList_.clear();
    children_.clear();
    ScrollToTop();
    DoLayout(rect_);
    Invalidate();
}

TreeNode* TreeView::GetVisibleNode(int index) const {
    if (index < 0 || index >= static_cast<int>(flatList_.size())) {
        return nullptr;
    }
    return flatList_[index];
}

void TreeView::ExpandAll(bool expand) {
    std::function<void(TreeNode*)> recurse = [&](TreeNode* n) {
        n->expanded_ = expand;
        for (auto& c : n->GetTreeChildren()) recurse(c.get());
    };
    for (auto& r : roots_) recurse(r.get());
    BuildFlatList();
    DoLayout(rect_);
    Invalidate();
}

void TreeView::FlattenNode(TreeNode* node,
                            std::vector<TreeNode*>& out, int level) {
    node->SetLevel(level);
    node->SetOwnerList(this);
    out.push_back(node);
    if (node->IsExpanded()) {
        for (auto& child : node->GetTreeChildren())
            FlattenNode(child.get(), out, level + 1);
    }
}

void TreeView::BuildFlatList() {
    TreeNode* selected = GetVisibleNode(GetCurSel());
    UnselectAll();
    flatList_.clear();
    children_.clear();  // reset container children to flat visible nodes

    for (auto& root : roots_)
        FlattenNode(root.get(), flatList_, 0);

    // Add flat nodes as container children so DoLayout / DoPaint sees them
    for (int i = 0; i < static_cast<int>(flatList_.size()); ++i) {
        flatList_[i]->SetIndex(i);
        flatList_[i]->SetFixedHeight(flatList_[i]->GetItemHeight());
        // Wrap in shared_ptr with a no-op deleter (nodes owned by roots_)
        Add(std::shared_ptr<TreeNode>(flatList_[i], [](TreeNode*) {}));
        if (flatList_[i] == selected) SelectItem(i, false);
    }
}

void TreeView::DoLayout(const wxRect& rc) {
    BuildFlatList();
    List::DoLayout(rc);
}

void TreeView::DoPaint(wxDC& dc, const wxRect& clipRect) {
    List::DoPaint(dc, clipRect);
}

bool TreeNode::OnKeyDown(int keyCode) {
    auto* tree = dynamic_cast<TreeView*>(ownerList_);
    return tree && tree->OnKeyDown(keyCode);
}

bool TreeView::OnKeyDown(int keyCode) {
    if (flatList_.empty()) return false;
    int index = GetCurSel();
    auto* node = GetVisibleNode(index);
    switch (keyCode) {
        case WXK_DOWN: index = std::min(index + 1, static_cast<int>(flatList_.size()) - 1); break;
        case WXK_UP: index = std::max(0, index - 1); break;
        case WXK_HOME: index = 0; break;
        case WXK_END: index = static_cast<int>(flatList_.size()) - 1; break;
        case WXK_RIGHT:
            if (!node) index = 0;
            else if (!node->GetTreeChildren().empty()) {
                if (!node->IsExpanded()) node->SetExpanded(true);
                else ++index;
            }
            break;
        case WXK_LEFT:
            if (!node) index = 0;
            else if (node->IsExpanded() && !node->GetTreeChildren().empty()) node->SetExpanded(false);
            else if (node->GetLevel() > 0) {
                while (index > 0 && flatList_[index]->GetLevel() >= node->GetLevel()) --index;
            }
            break;
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
            if (node) ActivateItem(index);
            return true;
        default: return false;
    }
    SelectItem(index);
    if (auto* selected = GetVisibleNode(index)) {
        const auto viewport = GetItemViewportRect();
        const auto rect = selected->GetRect();
        if (rect.GetTop() < viewport.GetTop())
            RestoreListScrollPos(GetListScrollPos() + rect.GetTop() - viewport.GetTop());
        else if (rect.GetBottom() > viewport.GetBottom())
            RestoreListScrollPos(GetListScrollPos() + rect.GetBottom() - viewport.GetBottom());
    }
    return true;
}

} // namespace wxui
