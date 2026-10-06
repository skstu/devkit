#pragma once
/// libwxui — List control family:
///   ListHeader, ListHeaderItem,
///   ListLabelElement, ListTextElement, ListContainerElement,
///   List

#include "layouts.hpp"
#include "label.hpp"

namespace wxui {

// ── ListHeader ────────────────────────────────────────────────────────────
class ListHeader : public HorizontalLayout {
public:
    std::string GetTag() const override { return "ListHeader"; }
};

// ── ListHeaderItem ────────────────────────────────────────────────────────
class ListHeaderItem : public Control {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "ListHeaderItem"; }

    void OnMouseEnter(const wxPoint&)   override;
    void OnMouseLeave()                 override;
    void OnMouseMove(const wxPoint&)    override;
    void OnButtonDown(const wxPoint&)   override;
    void OnButtonUp(const wxPoint&)     override;

    [[nodiscard]] bool IsDragable() const { return dragable_; }
    [[nodiscard]] int  GetSepWidth()const { return sepWidth_; }

private:
    int       hAlign_ = wxALIGN_CENTER_HORIZONTAL;
    bool      dragable_  = true;
    int       sepWidth_  = 4;
    ImageSpec normalImage_;
    ImageSpec hotImage_;
    ImageSpec pushedImage_;
    ImageSpec focusedImage_;
    ImageSpec sepImage_;
    wxColour  textColor_;
    bool      hot_    = false;
    bool      pushed_ = false;
    bool      resizing_ = false;
    int       resizeStartX_ = 0;
    int       resizeStartWidth_ = 0;
    ListHeaderItem* resizeTarget_ = nullptr;
};

// ── ListLabelElement ──────────────────────────────────────────────────────
class ListLabelElement : public Control {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "ListLabelElement"; }

    void OnMouseEnter(const wxPoint&)   override;
    void OnMouseLeave()                 override;
    void OnButtonDown(const wxPoint&)   override;
    void OnButtonUp(const wxPoint&)     override;
    void OnButtonDblClk(const wxPoint&) override;
    void OnRightButtonUp(const wxPoint&) override;

    void SetOwnerList(Control* list)  { ownerList_ = list; }
    void SetSelected(bool s);
    void SetIndex(int idx)            { index_ = idx; }
    void SetTextColor(const wxColour& color) { textColor_ = color; Invalidate(); }

    [[nodiscard]] bool   IsSelected() const { return selected_; }
    [[nodiscard]] int    GetIndex()   const { return index_; }
    [[nodiscard]] Control* GetOwnerList() { return ownerList_; }
    [[nodiscard]] const Control* GetOwnerList() const { return ownerList_; }
    [[nodiscard]] wxColour GetTextColor() const { return textColor_; }

protected:
    Control* ownerList_ = nullptr;
    bool     selected_  = false;
    bool     hot_       = false;
    int      index_     = -1;
    wxColour textColor_;
};

// ── ListTextElement ───────────────────────────────────────────────────────
class ListTextElement : public ListLabelElement {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    std::string GetTag() const override { return "ListTextElement"; }
    void OnButtonUp(const wxPoint& pt) override;

    void     SetColumnText(std::size_t col, const wxString& txt);
    void     SetColumnText(std::size_t col, std::string_view txt) {
        SetColumnText(col, Utf8ToWxString(txt));
    }
    wxString GetColumnText(std::size_t col) const;
    std::string GetColumnTextUtf8(std::size_t col) const {
        return WxStringToUtf8(GetColumnText(col));
    }
    [[nodiscard]] std::size_t GetColumnCount() const { return columns_.size(); }

private:
    std::vector<wxString> columns_;
};

// ── ListContainerElement ──────────────────────────────────────────────────
class ListContainerElement : public Container {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void DoLayout(const wxRect& rc) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "ListContainerElement"; }

    void OnMouseEnter(const wxPoint&)   override;
    void OnMouseLeave()                 override;
    void OnButtonDown(const wxPoint&)   override;
    void OnButtonUp(const wxPoint&)     override;
    void OnButtonDblClk(const wxPoint&) override;
    void OnRightButtonUp(const wxPoint&) override;

    void SetSelected(bool s);
    void SetIndex(int idx)   { index_ = idx; }
    void SetOwnerList(Control* list) { ownerList_ = list; }
    [[nodiscard]] Control* GetOwnerList() { return ownerList_; }
    [[nodiscard]] const Control* GetOwnerList() const { return ownerList_; }

    [[nodiscard]] bool     IsSelected() const { return selected_; }
    [[nodiscard]] int      GetIndex()   const { return index_; }

protected:
    Control* ownerList_ = nullptr;
    bool     selected_  = false;
    bool     hot_       = false;
    int      index_     = -1;
};

// ── List ──────────────────────────────────────────────────────────────────
class List : public VerticalLayout {
public:
    void DoLayout(const wxRect& rc)      override;
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "List"; }

    void OnMouseLeave() override;
    void OnMouseMove(const wxPoint& pt) override;
    void OnButtonDown(const wxPoint& pt) override;
    void OnButtonUp(const wxPoint& pt) override;
    bool OnMouseWheel(int rotation, int delta) override;
    void OnSetFocus() override;
    void OnKillFocus() override;

    // ── Header ────────────────────────────────────────────────────────
    void        SetHeader(ListHeader* hdr);
    const wxColour& ColumnLineColor() const { return columnLineColor_; }
    ListHeader* GetHeader() const { return header_; }
    [[nodiscard]] wxRect GetItemViewportRect() const;
    [[nodiscard]] wxRect GetHeaderViewportRect() const;

    // ── Dynamic rows ─────────────────────────────────────────────────
    // UI thread only. Nested batches perform one layout at the outer EndUpdate.
    void BeginUpdate();
    void EndUpdate();
    void ClearItems();
    // Create only visible rows plus one row on either side. Factory runs on the
    // UI thread; evicted rows and native children are released, never pooled forever.
    using VirtualRowFactory = std::function<std::shared_ptr<ListContainerElement>(int)>;
    void SetVirtualItems(int count, int rowHeight, VirtualRowFactory factory,
                         int scrollPosition = 0);
    ListTextElement* AppendTextItem(const std::vector<wxString>& columns,
                                    int height = 28);
    ListTextElement* AppendTextItemUtf8(const std::vector<std::string>& columns,
                                        int height = 28);
    ListContainerElement* AppendContainerItem(
        std::shared_ptr<ListContainerElement> item, int height = 28);

    // ── Selection ────────────────────────────────────────────────────
    void SelectItem(int idx, bool notify = true);
    void ActivateItem(int idx);
    void ClickItem(int idx, int x, int y, int column);
    void ShowItemContextMenu(int idx, int x, int y);
    void UnselectAll();
    [[nodiscard]] int GetCurSel()    const { return curSel_; }
    [[nodiscard]] int GetItemCount() const;
    [[nodiscard]] int GetListScrollPos() const { return scrollPos_; }
    [[nodiscard]] int GetListScrollRange() const { return vScrollRange_; }
    [[nodiscard]] int GetItemTop(int idx) const;
    void ScrollToTop();
    void RestoreListScrollPos(int pos);

    // ── Item style attributes ─────────────────────────────────────────
    wxColour itemTextColor_;
    wxColour itemBkColor_;
    wxColour itemSelTextColor_;
    wxColour itemSelBkColor_;
    wxColour itemHotTextColor_;
    wxColour itemHotBkColor_;
    wxColour itemLineColor_;
    wxColour columnLineColor_;
    int      itemFontId_   = -1;
    bool     itemShowHtml_ = false;

private:
    wxRect GetVScrollThumbRect() const;
    wxRect GetHScrollThumbRect() const;
    void SetListScrollPos(int pos);
    void SetListHScrollPos(int pos);
    void PaintVScrollBar(wxDC& dc) const;
    void PaintHScrollBar(wxDC& dc) const;

    void ItemsChanged();
    unsigned updateDepth_ = 0;
    bool itemsDirty_ = false;

    ListHeader* header_       = nullptr;
    bool        showHeader_   = true;
    bool        scrollSelect_ = false;
    int         curSel_       = -1;
    int         hScrollContentWidth_ = 0;
    int         vScrollRange_ = 0;
    int         vScrollPageSize_ = 0;
    wxRect      vScrollRect_;
    bool        vScrollVisible_ = false;
    bool        vScrollDragging_ = false;
    bool        vScrollThumbHot_ = false;
    int         vScrollDragOffset_ = 0;
    int         hScrollPos_ = 0;
    int         hScrollRange_ = 0;
    int         hScrollPageSize_ = 0;
    wxRect      hScrollRect_;
    bool        hScrollVisible_ = false;
    bool        hScrollDragging_ = false;
    bool        hScrollThumbHot_ = false;
    int         hScrollDragOffset_ = 0;
    std::string headerBkImage_;
    int stripeHeight_ = 0;
    wxColour stripeColor_;
    int virtualCount_ = -1, virtualHeight_ = 24;
    VirtualRowFactory virtualFactory_;
};

} // namespace wxui
