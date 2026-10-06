#pragma once
/// libwxui — Combo (drop-down) control.

#include "container.hpp"

class wxPopupTransientWindow;
class wxWindow;

namespace wxui {

class ComboPopupPanel;
class ComboPopupWindow;

class Combo : public Container {
public:
    ~Combo() override;
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "Combo"; }

    void OnManagerSet()              override;
    void SetRect(const wxRect& rc)    override;
    void SetVisible(bool v)           override;
    void SetEnabled(bool e)           override;
    void OnButtonDown(const wxPoint&)override;
    void OnButtonUp(const wxPoint&)  override;
    void OnMouseLeave()              override;
    bool OnKeyDown(int keyCode)      override;

    // ── Item management ────────────────────────────────────────────────
    void     AddItem(const wxString& text);
    void     AddItem(std::string_view text) { AddItem(Utf8ToWxString(text)); }
    void     InsertItem(int idx, const wxString& text);
    void     InsertItem(int idx, std::string_view text) { InsertItem(idx, Utf8ToWxString(text)); }
    void     RemoveItem(int idx);
    void     Clear();
    void     SetSelection(int idx, bool notify = false);

    [[nodiscard]] int       GetSelection()         const { return selection_; }
    [[nodiscard]] int       GetItemCount()         const;
    [[nodiscard]] wxString  GetItemText(int idx)   const;
    [[nodiscard]] std::string GetItemTextUtf8(int idx) const { return WxStringToUtf8(GetItemText(idx)); }
    [[nodiscard]] wxString  GetSelectedText()      const;
    [[nodiscard]] std::string GetSelectedTextUtf8() const { return WxStringToUtf8(GetSelectedText()); }

private:
    friend class ComboPopupPanel;
    friend class ComboPopupWindow;

    void CreateNativeCtrl();
    void SyncNativeCtrl();
    void SyncNativeItems();
    void ShowDropdown();
    void HideDropdown();
    void OnPopupSelect(int idx);
    void OnPopupDismissed();
    int  GetPopupItemHeight() const;
    int  GetPopupVisibleRows() const;
    void EnsurePopupHotVisible();

    // ── appearance attributes ──────────────────────────────────────────
    wxRect   textPadding_      {6, 0, 6, 0};
    wxSize   dropBoxSize_      {0, 150};
    int      itemFontId_       = -1;
    int      itemAlign_        = 1;   ///< 0=left 1=center 2=right
    wxRect   itemTextPadding_  {0, 0, 0, 0};
    wxColour itemTextColor_;
    wxColour itemBkColor_;
    wxColour itemSelTextColor_;
    wxColour itemSelBkColor_;
    wxColour itemHotTextColor_;
    wxColour itemHotBkColor_;
    wxColour itemDisTextColor_;
    bool     itemShowHtml_     = false;
    bool     multiExpanding_   = false;

    ImageSpec normalImage_;
    ImageSpec hotImage_;
    ImageSpec pushedImage_;
    ImageSpec disabledImage_;

    // ── state ──────────────────────────────────────────────────────────
    int                selection_  = -1;
    std::vector<wxString> items_;
    bool               dropped_    = false;
    int                hotIndex_    = -1;
    int                popupScrollOffset_ = 0;
    ::wxPopupTransientWindow* popup_ = nullptr;
    ::wxWindow*        popupPanel_  = nullptr;
};

} // namespace wxui
