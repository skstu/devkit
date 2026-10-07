#pragma once
#include "desktop.hpp"
#include "label.hpp"
#include "layouts.hpp"
#include "manager.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <wx/combobox.h>
#include <wx/weakref.h>
namespace wxui {
struct PaneBounds {
  int x = 0, y = 0, width = 0, height = 0;
};
struct PaneFrame {
  std::string name;
  PaneBounds bounds;
};
struct PaneLayout {
  bool tiled = true;
  std::vector<PaneFrame> panes;
};
// A native selector exposes its choices to accessibility inside modal forms.
class NativeChoice final : public wxui::Control {
  wxWeakRef<wxComboBox> native_;
  std::function<void(bool)> navigate_;
  bool keyboardFocus_ = false;
  void onManagerFocus(wxFocusEvent &event) {
    if (!keyboardFocus_)
      event.Skip();
  }
  void onManagerClick(wxMouseEvent &event) {
    keyboardFocus_ = false;
    event.Skip();
  }
  void onManagerKey(wxKeyEvent &event) {
    if (keyboardFocus_ && GetManager()->HasFocus() && IsVisible() &&
        IsEnabled() && !event.ControlDown() && !event.CmdDown() &&
        !event.AltDown()) {
      if (event.GetKeyCode() == WXK_TAB && navigate_) {
        keyboardFocus_ = false;
        navigate_(!event.ShiftDown());
        return;
      }
      if (event.GetKeyCode() == WXK_DOWN || event.GetKeyCode() == WXK_UP ||
          event.GetKeyCode() == WXK_SPACE || event.GetKeyCode() == WXK_RETURN) {
        native_->Popup();
        return;
      }
    }
    event.Skip();
  }
  void sync() {
    if (!native_)
      return;
    bool shown = IsVisible() && !GetRect().IsEmpty();
    for (auto *parent = GetParent(); parent; parent = parent->GetParent())
      shown &= parent->IsVisible();
    auto rect = GetRect();
    rect.Deflate(1);
    if (native_->GetRect() != rect)
      native_->SetSize(rect);
    native_->Show(shown);
    native_->Enable(IsEnabled());
  }

public:
  ~NativeChoice() override {
    if (GetManager()) {
      GetManager()->Unbind(wxEVT_SET_FOCUS, &NativeChoice::onManagerFocus,
                           this);
      GetManager()->Unbind(wxEVT_CHAR_HOOK, &NativeChoice::onManagerKey, this);
      GetManager()->Unbind(wxEVT_LEFT_DOWN, &NativeChoice::onManagerClick,
                           this);
    }
    if (native_)
      delete native_.get();
  }
  wxComboBox *native() const { return native_; }
  wxWindow *focusTarget() {
    return native_->AcceptsFocus() ? static_cast<wxWindow *>(native_.get())
                                   : GetManager();
  }
  void OnNavigate(std::function<void(bool)> action) {
    navigate_ = std::move(action);
  }
  void OnManagerSet() override {
    Control::OnManagerSet();
    if (!GetManager() || native_)
      return;
    native_ =
        new wxComboBox(GetManager(), wxID_ANY, wxEmptyString, wxDefaultPosition,
                       wxDefaultSize, 0, nullptr, wxCB_READONLY);
    GetManager()->Bind(wxEVT_SET_FOCUS, &NativeChoice::onManagerFocus, this);
    GetManager()->Bind(wxEVT_CHAR_HOOK, &NativeChoice::onManagerKey, this);
    GetManager()->Bind(wxEVT_LEFT_DOWN, &NativeChoice::onManagerClick, this);
    auto font = GetManager()->GetUIFont();
    font.SetPointSize(11);
    native_->SetFont(font);
    // A mouse-opened macOS popup may otherwise leave the text editor as
    // first responder, so its arrow keys reach the wrong form field.
    native_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
      OnSetFocus();
      event.Skip();
    });
    native_->Bind(wxEVT_COMBOBOX,
                  [this](const auto &) { FireNotify("selchanged"); });
    native_->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &event) {
      if (event.GetKeyCode() == WXK_TAB && !event.ControlDown() &&
          !event.CmdDown() && !event.AltDown() && navigate_) {
        navigate_(!event.ShiftDown());
        return;
      }
      event.Skip();
    });
    sync();
  }
  void SetRect(const wxRect &rect) override {
    Control::SetRect(rect);
    sync();
  }
  void SetVisible(bool visible) override {
    Control::SetVisible(visible);
    sync();
  }
  void SetEnabled(bool enabled) override {
    Control::SetEnabled(enabled);
    sync();
  }
  void DoPaint(wxDC &dc, const wxRect &clip) override {
    Control::DoPaint(dc, clip);
    sync();
    if (keyboardFocus_ && GetManager()->HasFocus()) {
      dc.SetBrush(*wxTRANSPARENT_BRUSH);
      dc.SetPen(wxPen(wxColour("#0066ff")));
      dc.DrawRectangle(GetRect());
    }
  }
  void OnSetFocus() override {
    if (!native_ || !IsVisible() || !IsEnabled())
      return;
    // macOS can exclude native popup buttons from keyboard focus.
    // Route this form's keys locally without changing system settings.
    keyboardFocus_ = !native_->AcceptsFocus();
    if (keyboardFocus_)
      GetManager()->SetFocusIgnoringChildren();
    else
      native_->SetFocus();
    Invalidate();
  }
  void AddItem(std::string_view text) {
    native_->Append(wxui::Utf8ToWxString(std::string(text)));
  }
  void Clear() { native_->Clear(); }
  int GetSelection() const { return native_->GetSelection(); }
  void SetSelection(int index, bool notify = false) {
    native_->SetSelection(index);
    if (notify)
      FireNotify("selchanged");
  }
  std::string GetSelectedTextUtf8() const {
    return wxui::WxStringToUtf8(native_->GetStringSelection());
  }
};
class FormEdit final : public wxui::Edit {
  wxWeakRef<wxTextCtrl> native_;
  std::function<void(bool)> step_;
  std::function<bool(bool)> navigate_;
  void clip();
  void raisePane();
  void navigateInput(bool forward);

public:
  void SetRect(const wxRect &rect) override {
    const auto old = native_ ? native_->GetRect() : wxRect();
    Edit::SetRect(rect);
    if (native_ && old != native_->GetRect() && manager_ &&
        !manager_->IsBeingDeleted())
      manager_->RefreshRect(old.Union(native_->GetRect()), false);
  }
  wxTextCtrl *native() const { return native_; }
  void OnStep(std::function<void(bool)> action) { step_ = std::move(action); }
  void OnNavigate(std::function<bool(bool)> action) {
    navigate_ = std::move(action);
  }
  void OnManagerSet() override {
    Edit::OnManagerSet();
    if (auto *input = NativeTextControl(); input && !native_) {
      native_ = input;
      input->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        if (IsEnabled())
          raisePane();
        e.Skip();
      });
      // Form fields are single-line values. Handle navigation
      // before the native editor can insert a literal tab or
      // pass Return to a dialog's default action.
      input->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &e) {
        const bool modified = e.ControlDown() || e.CmdDown() || e.AltDown();
        const auto key = e.GetKeyCode();
        if (key == WXK_TAB && !modified) {
          if (!navigate_ || !navigate_(!e.ShiftDown()))
            navigateInput(!e.ShiftDown());
          return;
        }
        if ((key == WXK_RETURN || key == WXK_NUMPAD_ENTER) && !modified) {
          if (IsEnabled())
            FireNotify("enter");
          return;
        }
        if ((key == WXK_UP || key == WXK_DOWN) && !modified && !e.ShiftDown() &&
            step_) {
          if (IsEnabled())
            step_(key == WXK_UP);
          return;
        }
        e.Skip();
      });
    }
  }
  void DoPaint(wxDC &dc, const wxRect &clipRect) override {
    Edit::DoPaint(dc, clipRect);
    clip();
  }
  void SetEnabled(bool enabled) override {
    if (enabled != IsEnabled())
      Edit::SetEnabled(enabled);
    clip();
  }
};
inline void FormEdit::navigateInput(bool forward) {
  if (!manager_)
    return;
  std::vector<wxui::Edit *> fields;
  const auto visit = [&](auto &&self, wxui::Control *control) -> void {
    if (!control || !control->IsVisible() || !control->IsEnabled())
      return;
    if (auto *edit = dynamic_cast<wxui::Edit *>(control);
        edit && edit->IsNativeWindowVisible()) {
      auto *form = dynamic_cast<FormEdit *>(edit);
      if (!form || (form->native() && form->native()->IsShown()))
        fields.push_back(edit);
    }
    if (auto *container = dynamic_cast<wxui::Container *>(control))
      for (const auto &child : container->GetChildren())
        self(self, child.get());
  };
  visit(visit, manager_->GetRoot());
  if (fields.empty() || (fields.size() == 1 && fields.front() == this)) {
    manager_->SetFocus();
    return;
  }
  const auto current = std::find(fields.begin(), fields.end(), this);
  const auto index = current == fields.end() ? (forward ? fields.size() - 1 : 0)
                                             : size_t(current - fields.begin());
  auto *next =
      fields[(index + (forward ? 1 : fields.size() - 1)) % fields.size()];
  next->OnSetFocus();
  if (auto *edit = dynamic_cast<FormEdit *>(next); edit && edit->native())
    edit->native()->SelectAll();
}
// Draggable/resizable panes. The consumer supplies layout policy and persists
// state.
class PaneCanvas : public wxui::Container {
  std::map<wxui::Control *, wxRect> frames_;
  wxui::Control *moving_ = nullptr;
  wxPoint start_;
  wxRect original_;
  int edges_ = 0;
  bool installed_ = false;
  bool tiled_ = true;
  int dragHeaderHeight_ = 32;

  std::function<void()> changed_;
  wxRect bounded(wxRect r, wxui::Control *c) const {
    const auto minimum = MinimumPaneSize(c->GetName());
    const int minw = minimum.width;
    r.width = std::clamp(r.width, std::max(1, std::min(minw, rect_.width)),
                         std::max(1, rect_.width));
    r.height = std::clamp(r.height,
                          std::max(1, std::min(minimum.height, rect_.height)),
                          std::max(1, rect_.height));
    r.x = std::clamp(r.x, 0, std::max(0, rect_.width - r.width));
    r.y = std::clamp(r.y, 0, std::max(0, rect_.height - r.height));
    return r;
  }

public:
  void OccludeEditors() {
    if (!manager_)
      return;
    // Native text fields are wx children, outside the drawn z-order. Hide
    // covered fields so they cannot show through or receive an accidental
    // click.
    std::function<void(wxui::Control *, size_t)> visit = [&](auto *control,
                                                             size_t index) {
      if (auto *edit = dynamic_cast<FormEdit *>(control);
          edit && edit->native()) {
        const auto rect = edit->native()->GetRect();
        bool covered = false;
        for (size_t i = index + 1; i < children_.size(); ++i)
          if (children_[i]->IsVisible() &&
              children_[i]->GetRect().Intersects(rect))
            covered = true;
        edit->native()->Show(edit->IsNativeWindowVisible() && !covered);
      }
      if (auto *box = dynamic_cast<wxui::Container *>(control))
        for (auto &child : box->GetChildren())
          visit(child.get(), index);
    };
    for (size_t i = 0; i < children_.size(); ++i)
      visit(children_[i].get(), i);
  }

private:
  void down(wxMouseEvent &e) {
    const auto pt = e.GetPosition();
    if (!rect_.Contains(pt)) {
      e.Skip();
      return;
    }
    wxui::Control *pane = nullptr;
    for (auto i = children_.rbegin(); i != children_.rend(); ++i)
      if ((*i)->IsVisible() && (*i)->GetRect().Contains(pt)) {
        pane = i->get();
        break;
      }
    if (!pane) {
      e.Skip();
      return;
    }
    const auto r = pane->GetRect();
    edges_ = 0;
    if (pt.x - r.x < 6)
      edges_ |= 1;
    if (r.GetRight() - pt.x < 6)
      edges_ |= 2;
    if (pt.y - r.y < 6)
      edges_ |= 4;
    if (r.GetBottom() - pt.y < 6)
      edges_ |= 8;
    auto *container = dynamic_cast<Container *>(pane);
    auto *hit = container ? container->FindControlByPoint(pt) : pane;
    const bool header =
        pt.y < r.y + dragHeaderHeight_ && hit &&
        (hit->GetTag() == "Label" || hit->GetTag() == "Control" ||
         hit->GetTag() == "HorizontalLayout");
    Raise(pane);
    if (!edges_ && !header) {
      e.Skip();
      return;
    }
    moving_ = pane;
    start_ = pt;
    original_ = frames_.at(pane);
    if (!manager_->HasCapture())
      manager_->CaptureMouse();
  }
  void motion(wxMouseEvent &e) {
    if (!moving_) {
      e.Skip();
      return;
    }
    auto d = e.GetPosition() - start_;
    auto r = original_;
    if (!edges_) {
      r.x += d.x;
      r.y += d.y;
    } else {
      if (edges_ & 1) {
        r.x += d.x;
        r.width -= d.x;
      }
      if (edges_ & 2)
        r.width += d.x;
      if (edges_ & 4) {
        r.y += d.y;
        r.height -= d.y;
      }
      if (edges_ & 8)
        r.height += d.y;
    }
    if (d == wxPoint())
      return;
    tiled_ = false;
    frames_[moving_] = bounded(r, moving_);
    DoLayout(rect_);
    manager_->Refresh();
  }
  void lost(wxMouseCaptureLostEvent &e) {
    moving_ = nullptr;
    e.Skip();
  }
  void up(wxMouseEvent &e) {
    if (!moving_) {
      e.Skip();
      return;
    }
    moving_ = nullptr;
    if (manager_->HasCapture())
      manager_->ReleaseMouse();
    if (changed_)
      changed_();
  }

public:
  void Install(std::function<void()> changed) {
    changed_ = std::move(changed);
    if (installed_)
      return;
    installed_ = true;
    manager_->Bind(wxEVT_LEFT_DOWN, &PaneCanvas::down, this);
    manager_->Bind(wxEVT_MOTION, &PaneCanvas::motion, this);
    manager_->Bind(wxEVT_LEFT_UP, &PaneCanvas::up, this);
    manager_->Bind(wxEVT_MOUSE_CAPTURE_LOST, &PaneCanvas::lost, this);
  }
  ~PaneCanvas() override {
    if (installed_ && manager_) {
      manager_->Unbind(wxEVT_LEFT_DOWN, &PaneCanvas::down, this);
      manager_->Unbind(wxEVT_MOTION, &PaneCanvas::motion, this);
      manager_->Unbind(wxEVT_LEFT_UP, &PaneCanvas::up, this);
      manager_->Unbind(wxEVT_MOUSE_CAPTURE_LOST, &PaneCanvas::lost, this);
    }
  }
  void Reset() {
    tiled_ = true;
    frames_.clear();
    std::stable_sort(children_.begin(), children_.end(),
                     [&](const auto &a, const auto &b) {
                       return PaneOrder(a->GetName()) < PaneOrder(b->GetName());
                     });
    for (const auto &c : children_)
      c->SetVisible(true);
    DoLayout(rect_);
    Invalidate();
    if (changed_)
      changed_();
  }
  PaneLayout LayoutState() const {
    PaneLayout result;
    result.tiled = tiled_;
    for (const auto &c : children_) {
      auto r = c->GetRect();
      r.Offset(-rect_.GetTopLeft());
      result.panes.push_back({c->GetName(), {r.x, r.y, r.width, r.height}});
    }
    return result;
  }
  void RestoreLayout(const PaneLayout &layout) {
    if (layout.panes.empty())
      return;
    tiled_ = layout.tiled;
    frames_.clear();
    std::vector<std::shared_ptr<Control>> ordered;
    std::set<Control *> seen;
    for (const auto &pane : layout.panes)
      for (const auto &c : children_)
        if (c->GetName() == pane.name && seen.insert(c.get()).second) {
          frames_[c.get()] = wxRect(pane.bounds.x, pane.bounds.y,
                                    pane.bounds.width, pane.bounds.height);
          ordered.push_back(c);
        }
    if (ordered.size() == children_.size())
      children_ = std::move(ordered);
    DoLayout(rect_);
    Invalidate();
  }
  void SetDragHeaderHeight(int pixels) {
    dragHeaderHeight_ = std::max(0, pixels);
  }

protected:
  virtual Extent MinimumPaneSize(const std::string &) const {
    return {100, 80};
  }
  virtual PaneBounds InitialPaneBounds(const std::string &, Extent size) const {
    return {0, 0, size.width, size.height};
  }
  virtual int PaneOrder(const std::string &) const { return 0; }

public:
  void Raise(wxui::Control *pane) {
    auto it = std::find_if(children_.begin(), children_.end(),
                           [&](auto &c) { return c.get() == pane; });
    if (it != children_.end() && it + 1 != children_.end()) {
      auto c = *it;
      children_.erase(it);
      children_.push_back(std::move(c));
      OccludeEditors();
      Invalidate();
    }
  }
  void DoLayout(const wxRect &rc) override {
    if (rc.width <= 0 || rc.height <= 0)
      return;
    for (auto &c : children_) {
      if (tiled_ || !frames_.contains(c.get())) {
        const auto box = InitialPaneBounds(c->GetName(), {rc.width, rc.height});
        frames_[c.get()] = wxRect(box.x, box.y, box.width, box.height);
      }
      auto r = bounded(frames_[c.get()], c.get());
      frames_[c.get()] = r;
      r.Offset(rc.GetTopLeft());
      c->SetRect(r);
    }
    OccludeEditors();
  }
  void DoPaint(wxDC &dc, const wxRect &clip) override {
    Container::DoPaint(dc, clip);
    dc.SetPen(wxPen(wxColour("#888888"), 1));
    for (const auto &c : children_)
      if (c->IsVisible()) {
        auto r = c->GetRect();
        for (int i = 5; i < 16; i += 5)
          dc.DrawLine(r.GetRight() - i, r.GetBottom() - 2, r.GetRight() - 2,
                      r.GetBottom() - i);
      }
    OccludeEditors();
  }
};
inline void FormEdit::clip() {
  for (auto *p = GetParent(); p; p = p->GetParent())
    if (auto *canvas = dynamic_cast<PaneCanvas *>(p)) {
      canvas->OccludeEditors();
      break;
    }
}
inline void FormEdit::raisePane() {
  auto *pane = GetParent();
  for (auto *p = GetParent(); p; p = p->GetParent()) {
    if (auto *canvas = dynamic_cast<PaneCanvas *>(p)) {
      canvas->Raise(pane);
      break;
    }
    pane = p;
  }
}
class ResizablePane final : public wxui::VerticalLayout {
public:
  void DoPaint(wxDC &dc, const wxRect &clip) override {
    VerticalLayout::DoPaint(dc, clip);
    dc.SetPen(wxPen(wxColour("#777777"), 1));
    const auto r = GetRect();
    for (int i = 3; i < 12; i += 3)
      dc.DrawLine(r.GetRight() - i, r.GetBottom() - 2, r.GetRight() - 2,
                  r.GetBottom() - i);
  }
};
class DecimalButton : public wxui::Button {
  bool active_ = false;
  int direction_ = 0;

public:
  void SetIndicator(bool active, int direction) {
    if (active_ == active && direction_ == direction)
      return;
    active_ = active;
    direction_ = direction;
    Invalidate();
  }
  void DoPaint(wxDC &dc, const wxRect &clip) override {
    const auto value = text_;
    text_.clear();
    Button::DoPaint(dc, clip);
    text_ = value;
    wxui::DecimalLabel label;
    label.SetManager(manager_);
    label.SetRect(rect_);
    label.SetText(value);
    label.SetAttribute("fontsize", "15");
    label.SetAttribute("align", "center");
    label.SetAttribute("textcolor", active_ ? "#000000" : "#aaaaaa");
    label.SetAttribute("disabledtextcolor", "#aaaaaa");
    label.SetEnabled(true);
    label.DoPaint(dc, clip);
    if (active_ && direction_) {
      const auto x = rect_.x + 5, y = rect_.y + 5;
      wxPoint points[3];
      if (direction_ > 0) {
        points[0] = {x, y + 6};
        points[1] = {x + 6, y + 6};
        points[2] = {x + 3, y};
      } else {
        points[0] = {x, y};
        points[1] = {x + 6, y};
        points[2] = {x + 3, y + 6};
      }
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.SetBrush(wxBrush(wxColour(direction_ > 0 ? "#009933" : "#cc0000")));
      dc.DrawPolygon(3, points);
    }
  }
};
class StackIconButton final : public wxui::Button {
public:
  void DoPaint(wxDC &dc, const wxRect &clip) override {
    Button::DoPaint(dc, clip);
    const auto r = GetRect();
    const int x = r.x + (r.width - 16) / 2, y = r.y + (r.height - 16) / 2;
    dc.SetPen(
        wxPen(IsEnabled() ? wxColour("#272727") : wxColour("#777777"), 1));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(x + 4, y, 12, 12);
    dc.SetBrush(wxBrush(wxColour("#c6cccc")));
    dc.DrawRectangle(x, y + 4, 12, 12);
    dc.DrawLine(x + 1, y + 7, x + 11, y + 7);
  }
};

} // namespace wxui
