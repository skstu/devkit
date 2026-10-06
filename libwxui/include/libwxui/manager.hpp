#pragma once
/// libwxui — UIManager: wxPanel that owns and drives the control tree.
///
/// Typical usage:
///   ControlFactory::RegisterBuiltins();
///   auto* mgr = new UIManager(parentFrame);
///   mgr->SetResourceRoot("res/");
///   mgr->LoadFromResource("ui/main.xml");
///   mgr->FindControl("ok")->Bind("click", [](auto&){ /* … */ });

#include "window.hpp"
#include "localization.hpp"

#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/timer.h>

#include <memory>
#include <functional>
#include <string>
#include <unordered_map>

// Forward-declare wxXmlDocument so the private BuildFromXmlDoc helper
// can reference it without pulling in <wx/xml/xml.h> in this header.
class wxXmlDocument;

namespace wxui {

class UIManager : public wxPanel {
    wxDECLARE_EVENT_TABLE();
public:
    using ResourceLoader = std::function<bool(const std::string&, std::string*)>;

    explicit UIManager(wxWindow*      parent,
                       wxWindowID     id    = wxID_ANY,
                       const wxPoint& pos   = wxDefaultPosition,
                       const wxSize&  size  = wxDefaultSize,
                       long           style = wxFULL_REPAINT_ON_RESIZE
                                            | wxWANTS_CHARS
                                            | wxCLIP_CHILDREN);
    ~UIManager() override;

    // ── Load ──────────────────────────────────────────────────────────
    bool LoadFromFile(const std::string& xmlPath);
    bool LoadFromFile(const wxString& xmlPath) { return LoadFromFile(WxStringToUtf8(xmlPath)); }
    bool LoadFromResource(const std::string& xmlPath);
    bool LoadFromResource(const wxString& xmlPath) { return LoadFromResource(WxStringToUtf8(xmlPath)); }
    bool LoadFromString(const std::string& xmlContent);
    bool LoadFromString(const wxString& xmlContent) { return LoadFromString(WxStringToUtf8(xmlContent)); }

    // ── Access ────────────────────────────────────────────────────────
    [[nodiscard]] Window*       GetRoot()         { return root_.get(); }
    [[nodiscard]] const Window* GetRoot()   const { return root_.get(); }

    Control* FindControl(const std::string& name);

    void LoadLanguageXml(const std::string& language, const std::string& xml);
    void SetLanguage(const std::string& language);
    void SetFallbackLanguage(const std::string& language);
    const Localization& GetLocalization() const { return localization_; }
    std::string Translate(const std::string& key, const std::string& fallback = {}) const {
        return localization_.Translate(key, fallback);
    }
    void RefreshTranslations();
    void UpdateTooltip();

    [[nodiscard]] wxSize GetPreferredSize(
        const wxSize& fallback = wxDefaultSize) const;
    [[nodiscard]] wxFont GetUIFont() const;
    void SetUIFont(const wxFont& font);

    // ── Resources ─────────────────────────────────────────────────────
    void SetResourceRoot(const std::string& dir) { resRoot_ = dir; }
    void SetResourceRoot(const wxString& dir) { resRoot_ = WxStringToUtf8(dir); }
    [[nodiscard]] const std::string& GetResourceRoot() const { return resRoot_; }
    void SetResourceLoader(ResourceLoader loader) { resourceLoader_ = std::move(loader); }
    bool LoadResourceBytes(const std::string& path, std::string* out) const;

    /// Load (and cache) a wxBitmap for the given path.
    wxBitmap LoadBitmap(const std::string& path);
    void     ClearImageCache();

    // ── Misc ──────────────────────────────────────────────────────────
    void RequestLayout();
    /// Run layout and repaint synchronously — use instead of SendSizeEvent()
    /// when the result must be visible before the next event-loop cycle.
    void ForceLayout();
    /// Render the custom control tree at logical 1x resolution on the UI thread.
    /// Native child windows (e.g. text inputs) and OS window chrome are excluded.
    [[nodiscard]] wxBitmap Snapshot(const wxRect& area);
    void InvalidateControl(Control* ctrl);
    void ForgetControlTree(Control* ctrl);

private:
    // ── wx event handlers ─────────────────────────────────────────────
    void OnPaint(wxPaintEvent&);
    void OnSize(wxSizeEvent&);
    void OnMouseMove(wxMouseEvent&);
    void OnMouseLeave(wxMouseEvent&);
    void OnLButtonDown(wxMouseEvent&);
    void OnLButtonUp(wxMouseEvent&);
    void OnLButtonDblClk(wxMouseEvent&);
    void OnRButtonDown(wxMouseEvent&);
    void OnRButtonUp(wxMouseEvent&);
    void OnMouseWheel(wxMouseEvent&);
    void OnKeyDown(wxKeyEvent&);
    void OnChar(wxKeyEvent&);
    void OnSetFocus(wxFocusEvent&);
    void OnKillFocus(wxFocusEvent&);
    void OnTimer(wxTimerEvent&);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent&);

    // ── Layout / Paint helpers ────────────────────────────────────────
    void DoLayout();
    void PaintTree(wxDC& dc, Container* container, const wxRect& clipRect);

    // ── Hit testing ───────────────────────────────────────────────────
    Control* HitTest(const wxPoint& pt, Container* root = nullptr);

    // ── Load helpers ──────────────────────────────────────────────────
    bool BuildFromXmlDoc(wxXmlDocument& doc);

    /// Called by ChildLayout::OnManagerSet to load and append child XML.
public:
    void LoadChildLayout(Container* container, const std::string& xmlFile);
private:

    // ── State ─────────────────────────────────────────────────────────
    std::shared_ptr<Window>                    root_;
    std::string                                resRoot_;
    ResourceLoader                             resourceLoader_;
    std::unordered_map<std::string, wxBitmap>  imageCache_;
    wxFont                                     uiFont_;
    Localization                               localization_;

    Control* hovered_  = nullptr;
    Control* pressed_  = nullptr;
    Control* focused_  = nullptr;

    wxTimer  timer_;
    bool     layoutPending_ = false;
};

} // namespace wxui
