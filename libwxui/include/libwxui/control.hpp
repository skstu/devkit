#pragma once
/// libwxui — base Control class (DUILib CControlUI equivalent).
///
/// Design: Controls are logical, owner-drawn objects. They have no OS handle
/// of their own. The UIManager (a real wxPanel) owns the tree, drives layout
/// and painting, and dispatches input events.

#include "types.hpp"
#include "image_spec.hpp"
#include "event.hpp"

#include <wx/dc.h>
#include <wx/gdicmn.h>

#include <memory>
#include <string>
#include <vector>
#include <map>

namespace wxui {

class Container;
class UIManager;
class Localization;

// ── Control ──────────────────────────────────────────────────────────────
class Control {
public:
    virtual ~Control() = default;

    // ── Identity ─────────────────────────────────────────────────────────
    [[nodiscard]] const std::string& GetName()      const { return name_; }
    [[nodiscard]] const std::string& GetUserData()  const { return userData_; }
    [[nodiscard]] const std::string& GetTooltip()   const { return tooltip_; }
    [[nodiscard]] const std::string& GetText()      const { return text_; }
    virtual std::string              GetTag()        const { return "Control"; }

    // ── Geometry ─────────────────────────────────────────────────────────
    [[nodiscard]] wxRect GetRect()       const { return rect_; }
    [[nodiscard]] int GetWidth() const { return rect_.width; }
    [[nodiscard]] int GetHeight() const { return rect_.height; }
    [[nodiscard]] wxRect GetPadding()    const { return padding_; }
    [[nodiscard]] wxSize GetFixedSize()  const { return fixedSize_; }
    [[nodiscard]] wxSize GetMinSize()    const { return minSize_; }
    [[nodiscard]] wxSize GetMaxSize()    const { return maxSize_; }
    [[nodiscard]] bool   IsFloat()       const { return float_; }
    [[nodiscard]] int GetLayoutWeight() const { return layoutWeight_; }

    virtual void SetRect(const wxRect& rc);
    void SetFixedWidth(int w)          { fixedSize_.x = w; }
    void SetFixedHeight(int h)         { fixedSize_.y = h; }
    void SetPadding(const wxRect& p)   { padding_ = p; }

    // ── Appearance ───────────────────────────────────────────────────────
    [[nodiscard]] bool IsVisible()     const { return visible_; }
    [[nodiscard]] bool IsEffectivelyVisible() const;
    [[nodiscard]] bool IsNativeWindowVisible() const;
    [[nodiscard]] bool IsEnabled()     const { return enabled_; }
    [[nodiscard]] bool IsMouseEnabled()const { return mouse_; }
    // Only the directly hit control opts in; interactive children do not inherit it.
    [[nodiscard]] bool IsWindowDragRegion() const { return windowDrag_; }
    [[nodiscard]] wxColour GetBkColor() const { return bkColor_; }

    virtual void SetVisible(bool v);
    virtual void SetEnabled(bool e);
    virtual void SetText(const std::string& t);
    virtual void SetText(const char* t);
    virtual void SetText(const wxString& t);

    // 仅绑定显示文案；输入框通常绑定 hint，避免切换语言覆盖用户输入。
    void BindTranslation(const std::string& attribute, const std::string& key,
                         const std::string& fallback = {});
    void UnbindTranslation(const std::string& attribute);
    void ApplyTranslations(const Localization& language);

    // ── Attributes (generic set / get) ───────────────────────────────────
    /// Apply a single XML attribute by name/value string.
    virtual void        SetAttribute(const std::string& key,
                                     const std::string& val);
    virtual std::string GetAttribute(const std::string& key) const;

    // ── Hierarchy ────────────────────────────────────────────────────────
    Container*       GetParent()              { return parent_; }
    const Container* GetParent()       const  { return parent_; }
    UIManager*       GetManager()             { return manager_; }
    const UIManager* GetManager()      const  { return manager_; }
    virtual void     SetManager(UIManager* m);

    /// Find a named control in this subtree (returns self if name matches).
    virtual Control* FindControl(const std::string& name);

    // ── Painting ─────────────────────────────────────────────────────────
    /// Paint this control into dc; only paint within clipRect.
    virtual void DoPaint(wxDC& dc, const wxRect& clipRect);

    /// Request a repaint of this control's rect.
    void Invalidate();

    // ── Events ───────────────────────────────────────────────────────────
    void Bind(const std::string& notify, NotifyCallback cb) {
        events_.Bind(notify, std::move(cb));
    }
    void Unbind(const std::string& notify) { events_.Unbind(notify); }

    // ── Input callbacks (called by UIManager) ────────────────────────────
    virtual void OnMouseEnter(const wxPoint&) {}
    virtual void OnMouseLeave()               {}
    virtual void OnMouseMove(const wxPoint&)  {}
    virtual void OnButtonDown(const wxPoint&) {}
    virtual void OnButtonUp(const wxPoint&)   {}
    virtual void OnButtonDblClk(const wxPoint&) {}
    virtual void OnRightButtonDown(const wxPoint&) {}
    virtual void OnRightButtonUp(const wxPoint&) {}
    virtual bool OnMouseWheel(int /*rotation*/, int /*delta*/) { return false; }
    virtual bool OnKeyDown(int /*keyCode*/)   { return false; }
    virtual bool OnChar(wxChar /*ch*/)        { return false; }
    virtual void OnSetFocus()                 { Invalidate(); }
    virtual void OnKillFocus()                { Invalidate(); }

protected:
    friend class Container;
    friend class UIManager;

    void             SetParent(Container* p)  { parent_ = p; }
    virtual void     OnManagerSet()           {}

    void FireNotify(const std::string& notify,
                    int x = 0, int y = 0,
                    intptr_t p1 = 0, const std::string& sp = {});

    // ── Drawing helpers ───────────────────────────────────────────────────
    void DrawBkColor(wxDC& dc);
    void DrawBkImage(wxDC& dc);
    void DrawBorder(wxDC& dc);

    // ── Stored attributes ─────────────────────────────────────────────────
    std::string name_;
    std::string tooltip_;
    std::string userData_;
    std::string text_;
    char        shortcut_        = 0;

    wxRect      rect_;
    wxRect      padding_         {0, 0, 0, 0};
    wxSize      fixedSize_       {0, 0};
    wxSize      minSize_         {0, 0};
    wxSize      maxSize_         {9999, 9999};
    int         layoutWeight_   = 1;

    bool bevel_ = false;
    wxColour    bkColor_;
    wxColour    bkColor2_;
    wxColour    bkColor3_;
    double      gradientEnd_ = 1.0;
    wxColour    borderColor_;
    wxColour    focusBorderColor_;
    wxSize      borderRound_     {0, 0};
    wxRect      cornerRadius_    {0, 0, 0, 0}; // top-left, top-right, bottom-right, bottom-left
    bool        customCorners_  = false;
    int         borderSize_      = 0;
    int         borderStyle_     = 0;
    int         leftBorder_      = 0;
    int         topBorder_       = 0;
    int         rightBorder_     = 0;
    int         bottomBorder_    = 0;
    ImageSpec   bkImage_;

    bool        enabled_         = true;
    bool        mouse_           = true;
    bool        windowDrag_      = false;
    bool        visible_         = true;
    bool        float_           = false;
    bool        colorHsl_        = false;
    bool        menu_            = false;
    bool        keyboard_        = true;

    Container*  parent_          = nullptr;
    UIManager*  manager_         = nullptr;
    EventSink   events_;
    struct TranslationBinding { std::string key, fallback; };
    std::map<std::string, TranslationBinding> translations_;
};

} // namespace wxui
