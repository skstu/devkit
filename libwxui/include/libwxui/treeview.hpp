#pragma once
/// libwxui — TreeView and TreeNode controls.

#include "list.hpp"

namespace wxui {

// ── TreeNode ──────────────────────────────────────────────────────────────
class TreeNode : public ListContainerElement {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "TreeNode"; }

    void OnButtonUp(const wxPoint& pt) override;
    bool OnKeyDown(int keyCode) override;

    // ── Tree structure ────────────────────────────────────────────────
    void SetLevel(int lvl)    { level_ = lvl; }
    int  GetLevel()     const { return level_; }

    void SetExpanded(bool e);
    bool IsExpanded()   const { return expanded_; }

    void SetChecked(bool c)   { checked_ = c; Invalidate(); }
    bool IsChecked()    const { return checked_; }

    void AddTreeChild(std::shared_ptr<TreeNode> child);
    void RemoveTreeChildren();
    [[nodiscard]] const std::vector<std::shared_ptr<TreeNode>>& GetTreeChildren() const {
        return treeChildren_;
    }

    // ── Calculated height in the flat list ───────────────────────────
    [[nodiscard]] int GetItemHeight() const { return itemHeight_; }
    void SetItemHeight(int h)         { itemHeight_ = h; }

private:
    wxRect FolderBtnRect() const;

    int   level_       = 0;
    bool  expanded_    = true;
    bool  checked_     = false;
    int   itemHeight_  = 24;

    std::vector<std::shared_ptr<TreeNode>> treeChildren_;

    // attribute strings for internal sub-element styling
    std::string horizAttr_;
    std::string dotLineAttr_;
    std::string folderAttr_;
    std::string checkboxAttr_;
    std::string itemAttr_;

    wxColour textColor_;
    wxColour selTextColor_;
    wxColour hotTextColor_;
    wxColour selHotTextColor_;

    friend class TreeView;  // allow ExpandAll to set expanded_ directly
};

// ── TreeView ──────────────────────────────────────────────────────────────
class TreeView : public List {
public:
    bool OnKeyDown(int keyCode) override;
    void DoLayout(const wxRect& rc) override;
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "TreeView"; }

    void AddRoot(std::shared_ptr<TreeNode> node);
    void RemoveAllRoots();

    void ExpandAll(bool expand = true);
    void CollapseAll()  { ExpandAll(false); }

    [[nodiscard]] const std::vector<std::shared_ptr<TreeNode>>& GetRoots() const {
        return roots_;
    }
    [[nodiscard]] TreeNode* GetVisibleNode(int index) const;

private:
    void BuildFlatList();
    void FlattenNode(TreeNode* node,
                     std::vector<TreeNode*>& out,
                     int level);

    bool     multipleItem_      = true;
    wxSize   itemCheckImgSize_  {0, 0};
    wxSize   itemIconImgSize_   {0, 0};
    bool     visibleFolderBtn_  = true;
    bool     visibleCheckBtn_   = false;
    unsigned itemMinWidth_      = 0;
    wxColour selItemTextColor_;
    wxColour selItemHotTextColor_;

    std::vector<std::shared_ptr<TreeNode>> roots_;
    std::vector<TreeNode*>                 flatList_;  ///< visible nodes in order
};

} // namespace wxui
