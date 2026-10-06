#pragma once
/// libwxui — Container (DUILib CContainerUI equivalent).
/// Manages a list of child Controls and routes layout / painting through them.

#include "control.hpp"

#include <memory>
#include <vector>

namespace wxui {

class Container : public Control {
public:
    using ChildList = std::vector<std::shared_ptr<Control>>;

    // ── Children ─────────────────────────────────────────────────────────
    void Add(std::shared_ptr<Control> child);
    void Remove(Control* child);
    void RemoveAll();
    void InsertAt(std::size_t idx, std::shared_ptr<Control> child);

    [[nodiscard]] const ChildList& GetChildren()  const { return children_; }
    [[nodiscard]] std::size_t      ChildCount()   const { return children_.size(); }
    Control*       GetChild(std::size_t idx)             { return idx < children_.size() ? children_[idx].get() : nullptr; }
    const Control* GetChild(std::size_t idx)       const { return idx < children_.size() ? children_[idx].get() : nullptr; }

    // ── Layout ───────────────────────────────────────────────────────────
    void SetRect(const wxRect& rc) override;

    /// Arrange children inside rc (called by SetRect and UIManager).
    virtual void DoLayout(const wxRect& rc);

    // ── Search ───────────────────────────────────────────────────────────
    Control* FindControl(const std::string& name)       override;
    Control* FindControlByPoint(const wxPoint& pt);

    // ── Painting ─────────────────────────────────────────────────────────
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;

    // ── Attributes ───────────────────────────────────────────────────────
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "Container"; }

    // ── Container-specific accessors ────────────────────────────────────
    [[nodiscard]] wxRect GetInset()        const { return inset_; }
    [[nodiscard]] int    GetChildPadding() const { return childPadding_; }
    [[nodiscard]] bool   HasVScrollBar()   const { return vScrollBar_; }
    [[nodiscard]] bool   HasHScrollBar()   const { return hScrollBar_; }
    [[nodiscard]] bool   IsMouseChild()    const { return mouseChild_; }

    void SetInset(const wxRect& r)   { inset_ = r; }
    void SetChildPadding(int p)      { childPadding_ = p; }

    int  GetScrollPos()              const { return scrollPos_; }
    void SetScrollPos(int pos);

    void SetManager(UIManager* m) override;

protected:
    void OnManagerSet() override;

    /// Propagate manager to all children.
    void PropagateManager(UIManager* m);

    ChildList children_;
    wxRect    inset_        {0, 0, 0, 0};
    bool      mouseChild_   = true;
    bool      vScrollBar_   = false;
    bool      hScrollBar_   = false;
    int       childPadding_ = 0;
    int       scrollPos_    = 0;
};

} // namespace wxui
