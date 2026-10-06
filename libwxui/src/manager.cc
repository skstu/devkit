#include <libwxui.hpp>
#include <wx/dcgraph.h>

#include <wx/dcbuffer.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/mstream.h>
#include <wx/sstream.h>
#include <wx/xml/xml.h>

#include <algorithm>

namespace wxui {

#ifdef __WXOSX__
bool BeginNativeWindowDrag(wxWindow* view);
#endif

namespace {

constexpr int kDefaultEditWidth = 360;
constexpr int kDefaultControlHeight = 24;

bool ControlTreeContains(const Control* root, const Control* target) {
    if (!root || !target) return false;
    if (root == target) return true;

    const auto* container = dynamic_cast<const Container*>(root);
    if (!container) return false;

    for (const auto& child : container->GetChildren()) {
        if (ControlTreeContains(child.get(), target)) return true;
    }

    if (const auto* treeNode = dynamic_cast<const TreeNode*>(root)) {
        for (const auto& child : treeNode->GetTreeChildren()) {
            if (ControlTreeContains(child.get(), target)) return true;
        }
    }
    return false;
}

List* OwningList(Control* control) {
    for (Control* current = control; current != nullptr;
         current = current->GetParent()) {
        if (auto* list = dynamic_cast<List*>(current)) return list;
    }
    return nullptr;
}

wxSize ClampMeasuredSize(const Control* control, wxSize size) {
    const wxSize fixed = control->GetFixedSize();
    if (fixed.x > 0) size.x = fixed.x;
    if (fixed.y > 0) size.y = fixed.y;

    const wxSize minSize = control->GetMinSize();
    size.x = std::max(size.x, minSize.x);
    size.y = std::max(size.y, minSize.y);

    const wxSize maxSize = control->GetMaxSize();
    if (maxSize.x > 0) size.x = std::min(size.x, maxSize.x);
    if (maxSize.y > 0) size.y = std::min(size.y, maxSize.y);
    return size;
}

wxSize TextExtent(const UIManager* manager, const Control* control) {
    if (!manager || control->GetText().empty()) return wxSize(0, 0);

    int width = 0;
    int height = 0;
    const_cast<UIManager*>(manager)->GetTextExtent(
        Utf8ToWxString(control->GetText()), &width, &height);
    return wxSize(width + 16, height + 8);
}

wxSize MeasureControl(const UIManager* manager, const Control* control);

wxSize MeasureVertical(const UIManager* manager, const Container* container) {
    const wxRect inset = container->GetInset();
    int width = 0;
    int height = inset.y + inset.height;
    int visibleCount = 0;

    for (const auto& child : container->GetChildren()) {
        if (!child->IsVisible()) continue;
        const wxSize childSize = MeasureControl(manager, child.get());
        width = std::max(width, childSize.x);
        height += childSize.y;
        ++visibleCount;
    }

    if (visibleCount > 1) {
        height += container->GetChildPadding() * (visibleCount - 1);
    }
    width += inset.x + inset.width;
    return wxSize(width, height);
}

wxSize MeasureHorizontal(const UIManager* manager, const Container* container) {
    const wxRect inset = container->GetInset();
    int width = inset.x + inset.width;
    int height = 0;
    int visibleCount = 0;

    for (const auto& child : container->GetChildren()) {
        if (!child->IsVisible()) continue;
        const wxSize childSize = MeasureControl(manager, child.get());
        width += childSize.x;
        height = std::max(height, childSize.y);
        ++visibleCount;
    }

    if (visibleCount > 1) {
        width += container->GetChildPadding() * (visibleCount - 1);
    }
    height += inset.y + inset.height;
    return wxSize(width, height);
}

wxSize MeasureContainer(const UIManager* manager, const Container* container) {
    if (dynamic_cast<const VerticalLayout*>(container) ||
        dynamic_cast<const Window*>(container)) {
        return MeasureVertical(manager, container);
    }
    if (dynamic_cast<const HorizontalLayout*>(container)) {
        return MeasureHorizontal(manager, container);
    }

    const wxRect inset = container->GetInset();
    int width = 0;
    int height = 0;
    for (const auto& child : container->GetChildren()) {
        if (!child->IsVisible()) continue;
        const wxSize childSize = MeasureControl(manager, child.get());
        width = std::max(width, childSize.x);
        height = std::max(height, childSize.y);
    }
    return wxSize(width + inset.x + inset.width,
                  height + inset.y + inset.height);
}

wxSize MeasureControl(const UIManager* manager, const Control* control) {
    if (!control || !control->IsVisible()) return wxSize(0, 0);

    wxSize measured(0, 0);
    if (const auto* container = dynamic_cast<const Container*>(control)) {
        measured = MeasureContainer(manager, container);
    } else if (dynamic_cast<const Edit*>(control)) {
        measured = TextExtent(manager, control);
        measured.x = std::max(measured.x, kDefaultEditWidth);
        measured.y = std::max(measured.y, kDefaultControlHeight);
    } else {
        measured = TextExtent(manager, control);
    }

    return ClampMeasuredSize(control, measured);
}

} // namespace

// ── Event table ───────────────────────────────────────────────────────────
wxBEGIN_EVENT_TABLE(UIManager, wxPanel)
    EVT_PAINT            (UIManager::OnPaint)
    EVT_SIZE             (UIManager::OnSize)
    EVT_MOTION           (UIManager::OnMouseMove)
    EVT_LEAVE_WINDOW     (UIManager::OnMouseLeave)
    EVT_LEFT_DOWN        (UIManager::OnLButtonDown)
    EVT_LEFT_UP          (UIManager::OnLButtonUp)
    EVT_LEFT_DCLICK      (UIManager::OnLButtonDblClk)
    EVT_RIGHT_DOWN       (UIManager::OnRButtonDown)
    EVT_RIGHT_UP         (UIManager::OnRButtonUp)
    EVT_MOUSEWHEEL       (UIManager::OnMouseWheel)
    EVT_KEY_DOWN         (UIManager::OnKeyDown)
    EVT_CHAR             (UIManager::OnChar)
    EVT_SET_FOCUS        (UIManager::OnSetFocus)
    EVT_KILL_FOCUS       (UIManager::OnKillFocus)
    EVT_MOUSE_CAPTURE_LOST(UIManager::OnMouseCaptureLost)
wxEND_EVENT_TABLE()

// ── ctor / dtor ───────────────────────────────────────────────────────────
UIManager::UIManager(wxWindow*      parent,
                     wxWindowID     id,
                     const wxPoint& pos,
                     const wxSize&  size,
                     long           style)
    : wxPanel(parent, id, pos, size, style)
    , timer_(this)
{
    if (parent) {
        SetFont(parent->GetFont());
        uiFont_ = parent->GetFont();
    }
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_TIMER, &UIManager::OnTimer, this);
}

UIManager::~UIManager() = default;

// ── Layout ────────────────────────────────────────────────────────────────
void UIManager::DoLayout() {
    if (!root_) return;
    wxRect rc = GetClientRect();
    root_->SetRect(rc);
    layoutPending_ = false;
}

// ── Paint ─────────────────────────────────────────────────────────────────
void UIManager::PaintTree(wxDC& dc, Container* container, const wxRect& clipRect) {
    for (auto& child : container->GetChildren()) {
        if (!child->IsVisible()) continue;
        wxRect childClip = clipRect;
        if (auto* list = dynamic_cast<List*>(container)) {
            if (child.get() == static_cast<Control*>(list->GetHeader())) {
                childClip = childClip.Intersect(list->GetHeaderViewportRect());
            } else {
                childClip = childClip.Intersect(list->GetItemViewportRect());
            }
        }
        const wxRect cr = childClip.Intersect(child->GetRect());
        if (cr.IsEmpty()) continue;
        wxDCClipper clipper(dc, cr);
        child->DoPaint(dc, cr);
        if (auto* sub = dynamic_cast<Container*>(child.get()))
            PaintTree(dc, sub, cr);
    }
}

void UIManager::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC bufferedDc(this);
    // macOS already provides a graphics context on its native paint DC.
    // Other backends acquire one wrapper for the entire pass.
    std::unique_ptr<wxGCDC> graphicsDc;
    if (!bufferedDc.GetGraphicsContext()) {
        graphicsDc = std::make_unique<wxGCDC>(bufferedDc);
        graphicsDc->SetBackground(bufferedDc.GetBackground());
    }
    wxDC& canvas = graphicsDc ? static_cast<wxDC&>(*graphicsDc) : static_cast<wxDC&>(bufferedDc);
    if (!root_) return;

    // Respect invalidated rows; a quote update must not repaint every visible control.
    const wxRect clip = GetUpdateRegion().GetBox().Intersect(GetClientRect());
    if (clip.IsEmpty()) return;
    wxDCClipper dirtyClip(canvas, clip);
    canvas.Clear();
    root_->DoPaint(canvas, clip);
    PaintTree(canvas, root_.get(), clip);
}

wxBitmap UIManager::Snapshot(const wxRect& area) {
    if (!root_ || area.width <= 0 || area.height <= 0 || !GetClientRect().Contains(area))
        return {};
    wxBitmap bitmap(area.width, area.height, 32);
    wxMemoryDC memory(bitmap);
    {
        wxGCDC canvas(memory);
        canvas.SetDeviceOrigin(-area.x, -area.y);
        canvas.SetBackground(wxBrush(GetBackgroundColour()));
        wxDCClipper clip(canvas, area);
        canvas.Clear();
        root_->DoPaint(canvas, area);
        PaintTree(canvas, root_.get(), area);
    }
    memory.SelectObject(wxNullBitmap);
    return bitmap;
}

// ── Size ──────────────────────────────────────────────────────────────────
void UIManager::OnSize(wxSizeEvent& evt) {
    DoLayout();
    Refresh();
    evt.Skip();
}

// ── Mouse ─────────────────────────────────────────────────────────────────
void UIManager::OnMouseMove(wxMouseEvent& evt) {
    const wxPoint pt = evt.GetPosition();
    Control* hit = HitTest(pt);

    if (hit != hovered_) {
        if (hovered_) hovered_->OnMouseLeave();
        hovered_ = hit;
        if (hovered_) hovered_->OnMouseEnter(pt);
        UpdateTooltip();
    } else if (hovered_ && hovered_ != pressed_) {
        hovered_->OnMouseMove(pt);
    }

    if (pressed_) pressed_->OnMouseMove(pt);
    evt.Skip();
}

void UIManager::OnMouseLeave(wxMouseEvent& evt) {
    if (hovered_) { hovered_->OnMouseLeave(); hovered_ = nullptr; }
    UpdateTooltip();
    evt.Skip();
}

void UIManager::OnLButtonDown(wxMouseEvent& evt) {
    const wxPoint pt = evt.GetPosition();
    Control* hit = HitTest(pt);
#ifdef __WXOSX__
    // Hand AppKit the original mouse-down before wx captures the pointer.
    if (hit && hit->IsEnabled() && hit->IsWindowDragRegion() && BeginNativeWindowDrag(this))
        return;
#endif
    pressed_ = hit;
    // Owner-drawn controls need keyboard focus after a native editor was used.
    SetFocus();

    List* oldList = OwningList(focused_);
    List* newList = OwningList(hit);
    if (focused_ && focused_ != hit &&
        !(oldList && oldList == newList && focused_ == oldList)) {
        focused_->OnKillFocus();
    }
    if (oldList && oldList != newList && oldList != focused_) {
        oldList->OnKillFocus();
    }
    focused_ = hit;
    if (newList && oldList != newList && newList != focused_) {
        newList->OnSetFocus();
    }
    if (focused_) focused_->OnSetFocus();

    if (pressed_) {
        if (!HasCapture()) CaptureMouse();
        pressed_->OnButtonDown(pt);
    }
    evt.Skip();
}

void UIManager::OnLButtonUp(wxMouseEvent& evt) {
    if (HasCapture()) ReleaseMouse();
    const wxPoint pt = evt.GetPosition();
    if (pressed_) {
        pressed_->OnButtonUp(pt);
        pressed_ = nullptr;
    }
    evt.Skip();
}

void UIManager::OnLButtonDblClk(wxMouseEvent& evt) {
    const wxPoint pt = evt.GetPosition();
    if (Control* hit = HitTest(pt)) hit->OnButtonDblClk(pt);
    evt.Skip();
}

void UIManager::OnRButtonDown(wxMouseEvent& evt) {
    const wxPoint pt = evt.GetPosition();
    Control* hit = HitTest(pt);

    List* oldList = OwningList(focused_);
    List* newList = OwningList(hit);
    if (focused_ && focused_ != hit &&
        !(oldList && oldList == newList && focused_ == oldList)) {
        focused_->OnKillFocus();
    }
    if (oldList && oldList != newList && oldList != focused_) {
        oldList->OnKillFocus();
    }
    focused_ = hit;
    if (newList && oldList != newList && newList != focused_) {
        newList->OnSetFocus();
    }
    if (focused_) focused_->OnSetFocus();

    if (hit) hit->OnRightButtonDown(pt);
    evt.Skip();
}

void UIManager::OnRButtonUp(wxMouseEvent& evt) {
    const wxPoint pt = evt.GetPosition();
    if (Control* hit = HitTest(pt)) hit->OnRightButtonUp(pt);
    evt.Skip();
}

void UIManager::OnMouseWheel(wxMouseEvent& evt) {
    const wxPoint pt = evt.GetPosition();
    for (Control* hit = HitTest(pt); hit != nullptr; hit = hit->parent_) {
        if (hit->OnMouseWheel(evt.GetWheelRotation(), evt.GetWheelDelta())) {
            return;
        }
    }
    evt.Skip();
}

void UIManager::OnMouseCaptureLost(wxMouseCaptureLostEvent&) {
    pressed_ = nullptr;
}

void UIManager::ForgetControlTree(Control* ctrl) {
    if (!ctrl) return;

    if (ControlTreeContains(ctrl, hovered_)) {
        hovered_ = nullptr;
        UpdateTooltip();
    }
    if (ControlTreeContains(ctrl, focused_)) {
        List* owningList = OwningList(ctrl);
        focused_ = owningList && owningList != ctrl ? owningList : nullptr;
    }
    if (ControlTreeContains(ctrl, pressed_)) {
        pressed_ = nullptr;
        if (HasCapture()) {
            ReleaseMouse();
        }
    }
}

// ── Keyboard ──────────────────────────────────────────────────────────────
void UIManager::OnKeyDown(wxKeyEvent& evt) {
    if (focused_ && focused_->OnKeyDown(evt.GetKeyCode())) return;
    evt.Skip();
}

void UIManager::OnChar(wxKeyEvent& evt) {
    if (focused_) focused_->OnChar(evt.GetKeyCode());
    evt.Skip();
}

// ── Focus ─────────────────────────────────────────────────────────────────
void UIManager::OnSetFocus(wxFocusEvent& evt) {
    if (focused_) focused_->OnSetFocus();
    evt.Skip();
}

void UIManager::OnKillFocus(wxFocusEvent& evt) {
    if (focused_) {
        List* oldList = OwningList(focused_);
        focused_->OnKillFocus();
        if (oldList && oldList != focused_) {
            oldList->OnKillFocus();
        }
        focused_ = nullptr;
    }
    evt.Skip();
}

// ── Timer ─────────────────────────────────────────────────────────────────
void UIManager::OnTimer(wxTimerEvent&) {
    if (layoutPending_) { DoLayout(); Refresh(); }
}

// ── Hit testing ───────────────────────────────────────────────────────────
Control* UIManager::HitTest(const wxPoint& pt, Container* node) {
    Container* search = node ? node : root_.get();
    if (!search) return nullptr;

    // Reverse-iterate (last child drawn is on top)
    const auto& children = search->GetChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        auto& child = *it;
        if (!child->IsVisible() || !child->IsMouseEnabled()) continue;
        if (auto* list = dynamic_cast<List*>(search)) {
            if (child.get() == static_cast<Control*>(list->GetHeader())) {
                if (!list->GetHeaderViewportRect().Contains(pt)) {
                    continue;
                }
            } else {
                if (!list->GetItemViewportRect().Contains(pt)) {
                    continue;
                }
            }
        }
        if (!child->GetRect().Contains(pt))                  continue;

        if (auto* sub = dynamic_cast<Container*>(child.get())) {
            Control* inner = HitTest(pt, sub);
            if (inner) return inner;
        }
        return child.get();
    }

    // Fall back to the search container itself
    if (search->GetRect().Contains(pt) && search->IsMouseEnabled())
        return search;
    return nullptr;
}

// ── FindControl ───────────────────────────────────────────────────────────
Control* UIManager::FindControl(const std::string& name) {
    if (!root_) return nullptr;
    return root_->FindControl(name);
}

wxFont UIManager::GetUIFont() const {
    return uiFont_.IsOk() ? uiFont_ : GetFont();
}

void UIManager::SetUIFont(const wxFont& font) {
    if (!font.IsOk()) return;
    uiFont_ = font;
    SetFont(font);
    if (root_) {
        root_->SetRect(GetClientRect());
    }
    Refresh();
}

wxSize UIManager::GetPreferredSize(const wxSize& fallback) const {
    if (!root_) return fallback;
    wxSize size = MeasureControl(this, root_.get());

    const wxSize initSize = root_->GetInitSize();
    if (initSize.x > 0) size.x = initSize.x;
    if (initSize.y > 0) size.y = initSize.y;

    const wxSize minInfo = root_->GetMinInfo();
    size.x = std::max(size.x, minInfo.x);
    size.y = std::max(size.y, minInfo.y);

    const wxSize maxInfo = root_->GetMaxInfo();
    if (maxInfo.x > 0) size.x = std::min(size.x, maxInfo.x);
    if (maxInfo.y > 0) size.y = std::min(size.y, maxInfo.y);

    if (fallback.x > 0) size.x = std::max(size.x, fallback.x);
    if (fallback.y > 0) size.y = std::max(size.y, fallback.y);
    return size;
}

// ── Image cache ───────────────────────────────────────────────────────────
wxBitmap UIManager::LoadBitmap(const std::string& path) {
    auto it = imageCache_.find(path);
    if (it != imageCache_.end()) return it->second;

    std::string bytes;
    if (LoadResourceBytes(path, &bytes)) {
        wxMemoryInputStream stream(bytes.data(), bytes.size());
        wxImage img;
        if (img.LoadFile(stream)) {
            wxBitmap bmp(img);
            imageCache_[path] = bmp;
            return bmp;
        }
    }

    const std::string full = resRoot_.empty() ? path : resRoot_ + "/" + path;
    wxImage img;
    if (!img.LoadFile(Utf8ToWxString(full))) {
        wxLogWarning("libwxui: image not found: %s", full.c_str());
        return wxNullBitmap;
    }
    wxBitmap bmp(img);
    imageCache_[path] = bmp;
    return bmp;
}

void UIManager::ClearImageCache() {
    imageCache_.clear();
}

bool UIManager::LoadResourceBytes(const std::string& path, std::string* out) const {
    if (!resourceLoader_) return false;
    return resourceLoader_(path, out);
}

void UIManager::RequestLayout() {
    if (layoutPending_) return;
    layoutPending_ = true;
    CallAfter([this] {
        if (!layoutPending_) return;
        DoLayout();
        Refresh();
    });
}

void UIManager::ForceLayout() {
    DoLayout();
    Refresh();
    Update();
}

void UIManager::InvalidateControl(Control* ctrl) {
    if (ctrl) RefreshRect(ctrl->GetRect(), false);
}

void UIManager::LoadChildLayout(Container* container, const std::string& xmlFile) {
    const std::string full = resRoot_.empty() ? xmlFile : resRoot_ + "/" + xmlFile;
    // Best-effort: load child XML via the existing LoadFromFile pipeline.
    // The child root will replace this UIManager's root, then its children
    // are transferred into the target container.
    wxXmlDocument doc;
    std::string xml;
    if (LoadResourceBytes(xmlFile, &xml)) {
        wxStringInputStream stream(Utf8ToWxString(xml));
        if (!doc.Load(stream)) {
            wxLogWarning("libwxui: ChildLayout cannot parse '%s'", xmlFile.c_str());
            return;
        }
    } else if (!doc.Load(Utf8ToWxString(full))) {
        wxLogWarning("libwxui: ChildLayout cannot load '%s'", full.c_str());
        return;
    }
    // Save current root, rebuild from child doc, transfer children, restore.
    auto savedRoot = root_;
    if (BuildFromXmlDoc(doc)) {
        if (root_) {
            for (auto& child : root_->GetChildren())
                container->Add(child);
        }
    }
    root_ = savedRoot;
}

} // namespace wxui
