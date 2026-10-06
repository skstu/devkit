#pragma once
/// libwxui — layout containers:
///   VerticalLayout, HorizontalLayout, TileLayout, TabLayout, ChildLayout

#include "container.hpp"

namespace wxui {

// ── VerticalLayout ────────────────────────────────────────────────────────
class VerticalLayout : public Container {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void DoLayout(const wxRect& rc) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "VerticalLayout"; }

    void OnMouseMove(const wxPoint& pt) override;
    void OnMouseLeave() override;
    void OnButtonDown(const wxPoint& pt) override;
    void OnButtonUp(const wxPoint& pt) override;

    [[nodiscard]] int  GetSepHeight() const { return sepHeight_; }
    [[nodiscard]] bool IsSepImm()     const { return sepImm_; }

private:
    [[nodiscard]] wxRect   SplitterRect() const;
    [[nodiscard]] Control* SplitterTarget() const;

    int  sepHeight_ = 0;   ///< >0 → separator at top; <0 → at bottom
    bool sepImm_    = false;
    bool resizing_ = false;
    int  resizeStartY_ = 0;
    int  resizeStartHeight_ = 0;
    Control* resizeTarget_ = nullptr;
};

// ── HorizontalLayout ──────────────────────────────────────────────────────
class HorizontalLayout : public Container {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void DoLayout(const wxRect& rc) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "HorizontalLayout"; }

    void OnMouseMove(const wxPoint& pt) override;
    void OnMouseLeave() override;
    void OnButtonDown(const wxPoint& pt) override;
    void OnButtonUp(const wxPoint& pt) override;

    [[nodiscard]] int  GetSepWidth() const { return sepWidth_; }
    [[nodiscard]] bool IsSepImm()    const { return sepImm_; }

private:
    [[nodiscard]] wxRect   SplitterRect() const;
    [[nodiscard]] Control* SplitterTarget() const;

    int  sepWidth_ = 0;    ///< >0 → separator on left; <0 → on right
    bool sepImm_   = false;
    bool resizing_ = false;
    int  resizeStartX_ = 0;
    int  resizeStartWidth_ = 0;
    Control* resizeTarget_ = nullptr;
};

// ── TileLayout ────────────────────────────────────────────────────────────
class TileLayout : public Container {
public:
    void DoLayout(const wxRect& rc) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "TileLayout"; }

    [[nodiscard]] int    GetColumns()  const { return columns_; }
    [[nodiscard]] wxSize GetItemSize() const { return itemSize_; }

private:
    int    columns_  = 1;
    wxSize itemSize_ {0, 0};
};

// ── TabLayout ─────────────────────────────────────────────────────────────
class TabLayout : public Container {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void DoLayout(const wxRect& rc) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "TabLayout"; }

    void SelectItem(const std::string& name);
    [[nodiscard]] const std::string& GetSelectedItem() const { return selectedId_; }

private:
    void ApplySelection();

    std::string selectedId_;
};

// ── ChildLayout ───────────────────────────────────────────────────────────
/// Loads and embeds a child XML layout file at runtime.
class ChildLayout : public Container {
public:
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "ChildLayout"; }

protected:
    void OnManagerSet() override;

private:
    std::string xmlFile_;
    bool        loaded_ = false;
};

} // namespace wxui
