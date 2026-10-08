#pragma once
// Optional standard-library-only C++ convenience layer. Every binary boundary
// remains the versioned C ABI; no C++ object is passed into the shared library.
#include "desktop.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace flui {
inline flui_string span(std::string_view s) { return {s.data(), s.size()}; }
inline std::string copy(flui_string s) {
  return s.data ? std::string(s.data, size_t(s.size)) : std::string{};
}
inline void check(flui_status s) {
  if (s != FLUI_OK)
    throw std::runtime_error("libflui error " + std::to_string(s));
}
namespace detail {
inline std::string json(std::string_view s) {
  std::string out = "\"";
  out.reserve(s.size() + 2);
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 32) {
        char b[7];
        std::snprintf(b, sizeof(b), "\\u%04x", c);
        out += b;
      } else
        out += char(c);
    }
  }
  return out + '"';
}
inline std::string attrs(const std::map<std::string, std::string> &a) {
  std::string s = "{";
  for (const auto &[k, v] : a) {
    if (s.size() > 1)
      s += ',';
    s += json(k) + ':' + json(v);
  }
  return s + '}';
}
inline std::vector<int> integers(std::string s) {
  std::replace(s.begin(), s.end(), ',', ' ');
  std::istringstream in(s);
  std::vector<int> out;
  int x;
  while (in >> x)
    out.push_back(x);
  return out;
}
inline uint64_t nextControl = 1;
inline std::function<void(std::function<void()>)> post =
    [](std::function<void()> f) {
      auto p = std::make_unique<std::function<void()>>(std::move(f));
      check(flui_dispatch(
          [](void *u) {
            std::unique_ptr<std::function<void()>> fn(
                static_cast<std::function<void()> *>(u));
            try {
              (*fn)();
            } catch (const std::exception &e) {
              std::fprintf(stderr, "libflui callback: %s\n", e.what());
            }
          },
          p.get()));
      p.release();
    };
} // namespace detail
struct Extent {
  int width = 0, height = 0;
};
struct Rect {
  int x = 0, y = 0, width = 0, height = 0;
};
struct Point {
  int x = 0, y = 0;
};
struct NotifyEvent {
  std::string type, strParam;
  int64_t param1 = 0, param2 = 0;
};
class Control;
struct FrameTiming {
  int buildUs = 0, rasterUs = 0, totalUs = 0;
};
struct WindowState : std::enable_shared_from_this<WindowState> {
  flui_window handle = 0;
  bool ready = false, closed = false, closing = false, scheduled = false,
       structure = true, inflight = false;
  std::vector<FrameTiming> frameTimings;
  unsigned batchDepth = 0;
  uint64_t request = 0, patches = 0, bytes = 0;
  std::string error;
  std::shared_ptr<Control> root;
  std::map<uint64_t, std::weak_ptr<Control>> controls, dirty;
  std::function<bool(bool)> close;
  std::function<void()> resized;
  bool main = false, layoutScheduled = false;
  void Schedule();
  void Flush();
  void ForceLayout() { Schedule(); }
  void Attach(const std::shared_ptr<Control> &);
  void Event(uint32_t, flui_status, std::string, std::string);
};
inline bool tooltipsEnabled = true;
inline std::vector<std::weak_ptr<WindowState>> windowStates;
class Control : public std::enable_shared_from_this<Control> {
protected:
  std::string tag_ = "Control";
  std::map<std::string, std::string> attributes_;
  std::vector<std::shared_ptr<Control>> children_;
  std::weak_ptr<WindowState> owner_;
  std::weak_ptr<Control> parent_;
  std::map<std::string, std::vector<std::function<void(const NotifyEvent &)>>>
      listeners_;
  Rect rect_{};
  void Changed(bool structure = false) {
    if (auto s = owner_.lock()) {
      if (structure)
        s->structure = true;
      else
        s->dirty[id] = weak_from_this();
      s->Schedule();
    }
  }

public:
  const uint64_t id = detail::nextControl++;
  virtual ~Control() = default;
  explicit Control(std::string tag = "Control") : tag_(std::move(tag)) {}
  virtual std::string Tag() const { return tag_; }
  virtual void SetAttribute(const std::string &key, const std::string &value) {
    if (attributes_.contains(key) && attributes_[key] == value)
      return;
    attributes_[key] = value;
    Changed();
  }
  std::string Attribute(const std::string &key,
                        std::string fallback = {}) const {
    auto p = attributes_.find(key);
    return p == attributes_.end() ? fallback : p->second;
  }
  virtual void SetText(const std::string &s) { SetAttribute("text", s); }
  std::string GetText() const { return Attribute("text"); }
  std::string GetName() const { return Attribute("name"); }
  void SetFixedWidth(int v) { SetAttribute("width", std::to_string(v)); }
  void SetFixedHeight(int v) { SetAttribute("height", std::to_string(v)); }
  Point GetFixedSize() const {
    return {std::atoi(Attribute("width").c_str()),
            std::atoi(Attribute("height").c_str())};
  }
  int GetWidth() const { return rect_.width; }
  int GetHeight() const { return rect_.height; }
  Rect GetRect() const { return rect_; }
  void SetVisible(bool b) { SetAttribute("visible", b ? "true" : "false"); }
  bool IsVisible() const { return Attribute("visible", "true") != "false"; }
  void SetEnabled(bool b) { SetAttribute("enabled", b ? "true" : "false"); }
  bool IsEnabled() const { return Attribute("enabled", "true") != "false"; }
  void SetFont(const std::string &font, double size) {
    SetAttribute("font", font);
    SetAttribute("fontsize", std::to_string(size));
  }
  void Invalidate() { Changed(); }
  virtual void Add(std::shared_ptr<Control> c) {
    if (!c)
      return;
    c->parent_ = weak_from_this();
    children_.push_back(c);
    if (auto s = owner_.lock())
      s->Attach(c);
    Changed(true);
  }
  void InsertAt(size_t n, std::shared_ptr<Control> c) {
    c->parent_ = weak_from_this();
    children_.insert(children_.begin() + std::min(n, children_.size()), c);
    if (auto s = owner_.lock())
      s->Attach(c);
    Changed(true);
  }
  void Detach() {
    owner_.reset();
    for (auto &c : children_)
      c->Detach();
  }
  void Remove(Control *c) {
    for (auto i = children_.begin(); i != children_.end(); ++i)
      if (i->get() == c) {
        (*i)->Detach();
        children_.erase(i);
        Changed(true);
        return;
      }
  }
  virtual void RemoveAll() {
    for (auto &c : children_)
      c->Detach();
    children_.clear();
    Changed(true);
  }
  Control *GetChild(size_t n) const {
    return n < children_.size() ? children_[n].get() : nullptr;
  }
  size_t ChildCount() const { return children_.size(); }
  const auto &GetChildren() const { return children_; }
  Control *FindControl(const std::string &name) const {
    if (GetName() == name)
      return const_cast<Control *>(this);
    for (auto &c : children_)
      if (auto *found = c->FindControl(name))
        return found;
    return nullptr;
  }
  void Bind(const std::string &event,
            std::function<void(const NotifyEvent &)> fn) {
    listeners_[event].push_back(std::move(fn));
    SetAttribute("event_" + event, "true");
  }
  void Notify(const std::string &event, std::string value = {}, int64_t p1 = 0,
              int64_t p2 = 0) {
    auto keep = weak_from_this().lock();
    auto i = listeners_.find(event);
    if (i == listeners_.end())
      return;
    const auto callbacks = i->second;
    for (auto &fn : callbacks)
      fn({event, value, p1, p2});
  }
  virtual void Receive(const std::string &event, const std::string &value) {
    if (event == "layout") {
      auto v = detail::integers(value);
      if (v.size() == 4) {
        rect_ = {v[0], v[1], v[2], v[3]};
        LayoutChanged();
      }
      return;
    }
    if (event == "click" && (!IsEnabled() || !IsVisible()))
      return;
    Notify(event, value);
  }
  virtual void LayoutChanged() {}
  WindowState *GetManager() const {
    auto s = owner_.lock();
    return s.get();
  }
  void DoLayout(Rect) {
    if (auto s = owner_.lock())
      s->Schedule();
  }
  int MeasureText(int width) const {
    const double font = std::atof(Attribute("fontsize", "13").c_str());
    int lines = 1, units = 0;
    for (unsigned char c : GetText()) {
      if (c == '\n') {
        ++lines;
        units = 0;
      } else if ((c & 0xc0) != 0x80) {
        units += c < 128 ? 1 : 2;
        if (units * font * .55 > std::max(1, width)) {
          ++lines;
          units = 0;
        }
      }
    }
    return int(std::ceil(lines * font * 1.25));
  }
  virtual std::string Serialize() const {
    std::string s = "{\"id\":" + detail::json(std::to_string(id)) +
                    ",\"tag\":" + detail::json(Tag()) +
                    ",\"attrs\":" + detail::attrs(attributes_) +
                    ",\"children\":[";
    for (auto &c : children_) {
      if (s.back() != '[')
        s += ',';
      s += c->Serialize();
    }
    return s + "]}";
  }
  std::string Patch() const {
    return "{\"id\":" + detail::json(std::to_string(id)) +
           ",\"attrs\":" + detail::attrs(attributes_) + '}';
  }
  friend struct WindowState;
  friend class List;
};
class Container : public Control {
public:
  explicit Container(std::string tag = "Container") : Control(std::move(tag)) {}
};
class VerticalLayout : public Container {
public:
  VerticalLayout() : Container("VerticalLayout") {}
};
class HorizontalLayout : public Container {
public:
  HorizontalLayout() : Container("HorizontalLayout") {}
};
class Label : public Control {
public:
  explicit Label(std::string tag = "Label") : Control(std::move(tag)) {}
};
class Button : public Label {
public:
  explicit Button(std::string tag = "Button") : Label(std::move(tag)) {};
  void OnButtonUp() { Receive("click", ""); }
};
class Icon : public Button {
public:
  Icon() : Button("Icon") {}
};
class StackIconButton : public Icon {
public:
  StackIconButton() { SetAttribute("glyph", "ticket"); }
};
class DecimalLabel : public Label {
public:
  DecimalLabel() : Label("DecimalLabel") {}
};
class DecimalButton : public Button {
public:
  DecimalButton() : Button("DecimalButton") {}
  void SetIndicator(bool live, int direction) {
    SetAttribute("live", live ? "true" : "false");
    SetAttribute("direction", std::to_string(direction));
  }
};
class Edit : public Control {
  uint64_t force_ = 0;

public:
  Edit() : Control("Edit") {}
  std::string GetValueUtf8() const { return Attribute("text"); }
  void SetValueUtf8(const std::string &s) {
    if (GetValueUtf8() == s)
      return;
    SetAttribute("text", s);
    SetAttribute("edit_revision", std::to_string(++force_));
    Notify("valuechanged", s);
  }
  void Clear() { SetValueUtf8(""); }
  void SelectAll() { SetAttribute("select_all", std::to_string(++force_)); }
  void OnSetFocus() { SetAttribute("focus", std::to_string(++force_)); }
  void OnNavigate(std::function<bool(bool)> f) {
    Bind("navigate", [this, f](const auto &e) {
      if (!f(e.strParam != "backward"))
        SetAttribute("traverse", e.strParam + std::to_string(++force_));
    });
  }
  void OnStep(std::function<void(bool)> f) {
    Bind("step", [f](const auto &e) { f(e.strParam == "up"); });
  }
  void Receive(const std::string &event, const std::string &value) override {
    if (event == "valuechanged") {
      attributes_["text"] = value;
      Notify(event, value);
      return;
    }
    Control::Receive(event, value);
  }
};
class FormEdit : public Edit {};
class Combo : public Control {
  std::vector<std::string> items_;
  int selected_ = -1;
  uint64_t focus_ = 0;
  void Sync() {
    std::string s = "[";
    for (auto &i : items_) {
      if (s.size() > 1)
        s += ',';
      s += detail::json(i);
    }
    SetAttribute("items_json", s + "]");
  }

public:
  Combo() : Control("Combo") {}
  void AddItem(std::string_view text) {
    items_.emplace_back(text);
    Sync();
  }
  int GetItemCount() const { return int(items_.size()); }
  void Clear() {
    items_.clear();
    selected_ = -1;
    Sync();
    SetAttribute("selected", "-1");
  }
  void SetSelection(int n, bool notify = false) {
    if (n < -1 || n >= int(items_.size()))
      return;
    selected_ = n;
    SetAttribute("selected", std::to_string(n));
    if (notify) {
      Notify("itemselect", std::to_string(n), n);
      Notify("selchanged", std::to_string(n), n);
    }
  }
  int GetSelection() const { return selected_; }
  std::string GetSelectedTextUtf8() const {
    return selected_ >= 0 && selected_ < int(items_.size()) ? items_[selected_]
                                                            : "";
  }
  void OnSetFocus() { SetAttribute("focus", std::to_string(++focus_)); }
  void OnNavigate(std::function<void(bool)> f) {
    Bind("navigate", [f](const auto &e) { f(e.strParam != "backward"); });
  }
  void Receive(const std::string &e, const std::string &v) override {
    if (e == "itemselect") {
      const int n = std::atoi(v.c_str());
      if (n >= 0 && n < int(items_.size())) {
        SetSelection(n);
        Notify(e, v, n);
        Notify("selchanged", v, n);
      }
      return;
    }
    Control::Receive(e, v);
  }
};
class NativeChoice : public Combo {};
class Option : public Button {
public:
  Option() : Button("Option") {}
  bool IsSelected() const { return Attribute("selected") == "true"; }
  void SetSelected(bool b) { SetAttribute("selected", b ? "true" : "false"); }
  void Receive(const std::string &e, const std::string &v) override {
    if (e == "click" && IsEnabled())
      SetSelected(!IsSelected());
    Control::Receive(e, v);
  }
};
class TabLayout : public Container {
public:
  TabLayout() : Container("TabLayout") {}
  void SelectItem(const std::string &id) { SetAttribute("selectedid", id); }
};
class ListHeader : public HorizontalLayout {
public:
  ListHeader() { tag_ = "ListHeader"; }
};
class ListHeaderItem : public Label {};
class ListContainerElement : public HorizontalLayout {
public:
  ListContainerElement() { tag_ = "ListRow"; }
};
class ListTextElement : public ListContainerElement {
public:
  std::string GetColumnTextUtf8(size_t i) const {
    return i < children_.size() ? children_[i]->GetText() : "";
  }
};
class List : public Container {
  ListHeader *header_ = nullptr;
  int count_ = 0, rowHeight_ = 24, scroll_ = 0, selection_ = -1, first_ = -1,
      visibleCount_ = 0;
  bool virtual_ = false;
  std::function<std::shared_ptr<Control>(int)> factory_;
  std::map<int, std::shared_ptr<Control>> materialized_;
  void Materialize(int first, int count) {
    if (!virtual_ || !factory_)
      return;
    first = std::clamp(first, 0, std::max(0, count_ - 1));
    count = std::clamp(count, 1, 128);
    if (first == first_ && count == visibleCount_)
      return;
    first_ = first;
    visibleCount_ = count;
    for (auto it = materialized_.begin(); it != materialized_.end();)
      if (it->first < first || it->first >= first + count) {
        it->second->Detach();
        it = materialized_.erase(it);
      } else
        ++it;
    for (int i = first; i < std::min(count_, first + count); ++i)
      if (!materialized_.contains(i)) {
        auto row = factory_(i);
        row->parent_ = weak_from_this();
        row->SetAttribute("index", std::to_string(i));
        row->SetFixedHeight(rowHeight_);
        materialized_[i] = row;
        if (auto s = owner_.lock())
          s->Attach(row);
      }
    std::shared_ptr<Control> header;
    for (auto &c : children_)
      if (c.get() == header_)
        header = c;
    children_.clear();
    if (header)
      children_.push_back(header);
    for (auto &[i, row] : materialized_)
      children_.push_back(row);
    Changed(true);
  }

public:
  List() : Container("List") {}
  void BeginUpdate() {
    if (auto s = owner_.lock())
      ++s->batchDepth;
  }
  void EndUpdate() {
    if (auto s = owner_.lock(); s && s->batchDepth)
      --s->batchDepth;
    Changed();
  }
  void RemoveAll() override {
    SetHeader(nullptr);
    Control::RemoveAll();
    materialized_.clear();
    factory_ = {};
    virtual_ = false;
    count_ = 0;
    first_ = -1;
    selection_ = -1;
    SetAttribute("virtual_count", "0");
    SetAttribute("selected", "-1");
  }
  void ClearItems() {
    if (header_) {
      auto keep = std::find_if(children_.begin(), children_.end(),
                               [this](auto &c) { return c.get() == header_; });
      auto saved = keep != children_.end() ? *keep : nullptr;
      RemoveAll();
      if (saved) {
        Add(saved);
        SetHeader(static_cast<ListHeader *>(saved.get()));
      }
    } else
      RemoveAll();
    virtual_ = false;
    factory_ = {};
    materialized_.clear();
    count_ = 0;
    first_ = -1;
    selection_ = -1;
    SetAttribute("virtual_count", "0");
    SetAttribute("selected", "-1");
  }
  void SetHeader(ListHeader *h) {
    header_ = h;
    SetAttribute("header", h ? std::to_string(h->id) : "");
  }
  ListHeader *GetHeader() const { return header_; }
  void SetVirtualItems(int count, int height,
                       std::function<std::shared_ptr<Control>(int)> factory,
                       int scroll = 0) {
    ClearItems();
    virtual_ = true;
    count_ = std::max(0, count);
    rowHeight_ = std::max(1, height);
    factory_ = std::move(factory);
    scroll_ = scroll;
    SetAttribute("virtual_count", std::to_string(count_));
    SetAttribute("row_height", std::to_string(rowHeight_));
    Materialize(scroll_ / rowHeight_,
                std::clamp(rect_.height / rowHeight_ + 3, 3, 80));
  }
  int GetItemCount() const {
    return virtual_ ? count_ : int(children_.size()) - (header_ ? 1 : 0);
  }
  int GetCurSel() const { return selection_; }
  void SelectItem(int n, bool notify = true) {
    selection_ = n;
    SetAttribute("selected", std::to_string(n));
    if (notify)
      Notify("itemselect", std::to_string(n), n);
  }
  int GetListScrollPos() const { return scroll_; }
  int GetListScrollRange() const {
    int height = 0;
    if (virtual_)
      height = count_ * rowHeight_;
    else
      for (auto &c : children_)
        if (c.get() != header_)
          height += std::max(1, c->GetFixedSize().y);
    return std::max(0, height - GetItemViewportRect().height);
  }
  Rect GetItemViewportRect() const {
    return {0, 0, rect_.width,
            std::max(0, rect_.height - (header_ ? header_->GetHeight() : 0))};
  }
  void RestoreListScrollPos(int n) {
    scroll_ = std::clamp(n, 0, GetListScrollRange());
    SetAttribute("scroll", std::to_string(scroll_));
    if (virtual_)
      Materialize(scroll_ / rowHeight_,
                  std::clamp(rect_.height / rowHeight_ + 3, 3, 80));
  }
  ListTextElement *AppendTextItemUtf8(const std::vector<std::string> &texts,
                                      int height = 24) {
    auto row = std::make_shared<ListTextElement>();
    row->SetFixedHeight(height);
    for (size_t i = 0; i < texts.size(); ++i) {
      auto c = std::make_shared<Label>();
      c->SetText(texts[i]);
      c->SetAttribute("textpadding", "5,0,5,0");
      if (header_ && header_->GetChild(i)) {
        const auto *h = header_->GetChild(i);
        c->SetFixedWidth(h->GetFixedSize().x);
      }
      row->Add(c);
    }
    Add(row);
    return row.get();
  }
  std::shared_ptr<ListContainerElement> AppendContainerItem(int height = 24) {
    auto row = std::make_shared<ListContainerElement>();
    row->SetFixedHeight(height);
    Add(row);
    return row;
  }
  void AppendContainerItem(std::shared_ptr<ListContainerElement> row,
                           int height) {
    row->SetFixedHeight(height);
    Add(row);
  }
  void Receive(const std::string &e, const std::string &v) override {
    if (e == "viewport") {
      const auto n = detail::integers(v);
      if (n.size() == 3) {
        scroll_ = n[2];
        Materialize(n[0], n[1]);
        Notify("scroll", v);
      }
      return;
    }
    if (e == "itemclick") {
      auto n = detail::integers(v);
      if (n.size() == 2) {
        selection_ = n[0];
        Notify("itemclick", std::to_string(n[1]), n[0], n[1]);
      }
      return;
    }
    Control::Receive(e, v);
  }
};
struct PaneBounds {
  int x = 0, y = 0, width = 0, height = 0;
};
struct PaneLayout {
  bool tiled = true;
  struct Pane {
    std::string name;
    PaneBounds bounds;
  };
  std::vector<Pane> panes;
};
class ResizablePane : public VerticalLayout {
public:
  ResizablePane() { tag_ = "Pane"; }
};
class PaneCanvas : public Container {
  std::function<void()> changed_;
  bool tiled_ = true;
  Extent last_{};

protected:
  virtual Extent MinimumPaneSize(const std::string &) const {
    return {160, 120};
  }
  virtual int PaneOrder(const std::string &) const { return 0; }
  virtual PaneBounds InitialPaneBounds(const std::string &, Extent size) const {
    return {0, 0, size.width, size.height};
  }

public:
  PaneCanvas() : Container("PaneCanvas") {}
  void SetDragHeaderHeight(int v) {
    SetAttribute("drag_height", std::to_string(v));
  }
  void Install(std::function<void()> fn) { changed_ = std::move(fn); }
  void Reset() {
    tiled_ = true;
    last_ = {};
    LayoutChanged();
  }
  void Raise(Control *pane) {
    auto i = std::find_if(children_.begin(), children_.end(),
                          [pane](auto &c) { return c.get() == pane; });
    if (i != children_.end()) {
      auto c = *i;
      children_.erase(i);
      children_.push_back(c);
      Changed(true);
    }
  }
  PaneLayout LayoutState() const {
    PaneLayout out;
    out.tiled = tiled_;
    for (auto &c : children_) {
      auto v = detail::integers(c->Attribute("bounds"));
      if (v.size() == 4)
        out.panes.push_back({c->GetName(), {v[0], v[1], v[2], v[3]}});
    }
    return out;
  }
  void RestoreLayout(const PaneLayout &state) {
    tiled_ = state.tiled;
    for (auto &p : state.panes)
      if (auto *c = FindControl(p.name))
        c->SetAttribute("bounds", std::to_string(p.bounds.x) + "," +
                                      std::to_string(p.bounds.y) + "," +
                                      std::to_string(p.bounds.width) + "," +
                                      std::to_string(p.bounds.height));
  }
  void LayoutChanged() override {
    if (rect_.width <= 0 || rect_.height <= 0)
      return;
    Extent size{rect_.width, rect_.height};
    if (tiled_ && (size.width != last_.width || size.height != last_.height)) {
      last_ = size;
      for (auto &c : children_) {
        const auto r = InitialPaneBounds(c->GetName(), size);
        const auto min = MinimumPaneSize(c->GetName());
        c->SetAttribute("bounds", std::to_string(r.x) + "," +
                                      std::to_string(r.y) + "," +
                                      std::to_string(r.width) + "," +
                                      std::to_string(r.height));
        c->SetAttribute("minimum", std::to_string(min.width) + "," +
                                       std::to_string(min.height));
      }
    }
    if (changed_)
      changed_();
  }
  void Receive(const std::string &e, const std::string &v) override {
    if (e == "pane") {
      const auto split = v.find(':');
      if (split != v.npos)
        if (auto *c = FindControl(v.substr(0, split))) {
          tiled_ = false;
          c->SetAttribute("bounds", v.substr(split + 1));
          if (changed_)
            changed_();
        }
      return;
    }
    Control::Receive(e, v);
  }
};
class PricePlot : public Control {
public:
  struct Bar {
    std::string date, open, high, low, close;
  };
  PricePlot() : Control("Plot") {}
  void SetCandles(bool b) { SetAttribute("candles", b ? "true" : "false"); }
  void SetBars(const std::vector<Bar> &bars) {
    std::string s = "[";
    for (auto &b : bars) {
      if (s.size() > 1)
        s += ',';
      s += '[' + detail::json(b.date) + ',' + detail::json(b.open) + ',' +
           detail::json(b.high) + ',' + detail::json(b.low) + ',' +
           detail::json(b.close) + ']';
    }
    SetAttribute("bars", s + "]");
  }
  void
  SetSnapshot(const std::vector<std::pair<std::string, std::string>> &data) {
    std::string s = "[";
    for (auto &[label, value] : data) {
      if (s.size() > 1)
        s += ',';
      s += '[' + detail::json(label) + ',' + detail::json(value) + ']';
    }
    SetAttribute("snapshot", s + "]");
  }
  void Receive(const std::string &e, const std::string &v) override {
    if (e == "pointchange") {
      Notify(e, v, std::atoi(v.c_str()));
      return;
    }
    Control::Receive(e, v);
  }
};
class ControlFactory {
  std::map<std::string, std::function<std::shared_ptr<Control>()>> factories_;

public:
  static ControlFactory &Instance() {
    static ControlFactory f;
    return f;
  }
  void Register(std::string name,
                std::function<std::shared_ptr<Control>()> factory) {
    factories_[std::move(name)] = std::move(factory);
  }
  std::shared_ptr<Control> Create(const std::string &tag) {
    if (auto i = factories_.find(tag); i != factories_.end())
      return i->second();
#define FLUI_CREATE(T)                                                         \
  if (tag == #T)                                                               \
    return std::make_shared<T>();
    FLUI_CREATE(Control)
    FLUI_CREATE(Container)
    FLUI_CREATE(VerticalLayout)
    FLUI_CREATE(HorizontalLayout) FLUI_CREATE(Label) FLUI_CREATE(Button)
        FLUI_CREATE(Icon) FLUI_CREATE(DecimalLabel) FLUI_CREATE(DecimalButton)
            FLUI_CREATE(Edit) FLUI_CREATE(Combo) FLUI_CREATE(Option)
                FLUI_CREATE(TabLayout) FLUI_CREATE(List) FLUI_CREATE(PricePlot)
#undef FLUI_CREATE
                    if (tag ==
                        "Window") return std::make_shared<Container>("Window");
    throw std::runtime_error("Unsupported libflui control: " + tag);
  }
};
inline std::shared_ptr<Control> CreateControlFromXml(std::string_view xml) {
  struct Parse {
    std::vector<std::shared_ptr<Control>> stack;
    std::shared_ptr<Control> root;
    std::string error;
  } parser;
  auto status = flui_xml_visit(
      span(xml),
      [](uint32_t kind, flui_string name, flui_string value,
         void *user) -> int32_t {
        auto &p = *static_cast<Parse *>(user);
        try {
          if (kind == 1) {
            auto c = ControlFactory::Instance().Create(copy(name));
            if (p.stack.empty())
              p.root = c;
            else
              p.stack.back()->Add(c);
            p.stack.push_back(c);
          } else if (kind == 2)
            p.stack.back()->SetAttribute(copy(name), copy(value));
          else if (kind == 3)
            p.stack.pop_back();
          return 0;
        } catch (const std::exception &e) {
          p.error = e.what();
          return 1;
        }
      },
      &parser);
  if (status != FLUI_OK || !parser.root)
    throw std::runtime_error(parser.error.empty() ? "Invalid libflui XML"
                                                  : parser.error);
  return parser.root;
}
inline void WindowState::Attach(const std::shared_ptr<Control> &c) {
  c->owner_ = shared_from_this();
  controls[c->id] = c;
  for (auto &child : c->children_)
    Attach(child);
}
inline void WindowState::Schedule() {
  if (closed || closing || scheduled || !ready || inflight || batchDepth)
    return;
  scheduled = true;
  std::weak_ptr<WindowState> weak = shared_from_this();
  detail::post([weak] {
    if (auto s = weak.lock()) {
      s->scheduled = false;
      s->Flush();
    }
  });
}
inline void WindowState::Flush() {
  if (closed || closing || !ready || inflight || batchDepth ||
      (!structure && dirty.empty()))
    return;
  const bool full = structure;
  std::string data;
  if (full) {
    for (auto i = controls.begin(); i != controls.end();)
      if (i->second.expired())
        i = controls.erase(i);
      else
        ++i;
    data = root->Serialize();
  } else {
    data = "[";
    for (auto &[id, w] : dirty)
      if (auto c = w.lock())
        if (c->owner_.lock().get() == this) {
          if (data.size() > 1)
            data += ',';
          data += c->Patch();
        }
    data += ']';
  }
  const auto code = full ? flui_window_set_tree(handle, span(data), ++request)
                         : flui_window_patch(handle, span(data), ++request);
  if (code == FLUI_BUSY)
    return;
  check(code);
  inflight = true;
  structure = false;
  dirty.clear();
  bytes += data.size();
  ++patches;
}
inline void WindowState::Event(uint32_t kind, flui_status status,
                               std::string name, std::string value) {
  if (closed)
    return;
  if (kind == FLUI_EVENT_READY) {
    ready = true;
    Schedule();
    return;
  }
  if (kind == FLUI_EVENT_COMPLETE) {
    inflight = false;
    if (status != FLUI_OK) {
      error = value;
      std::fprintf(stderr, "libflui document: %s\n", value.c_str());
    } else
      Schedule();
    return;
  }
  if (kind == FLUI_EVENT_ERROR) {
    error = value;
    std::fprintf(stderr, "libflui renderer: %s\n", value.c_str());
    return;
  }
  if (kind == FLUI_EVENT_CLOSE_REQUEST) {
    if (!close || close(true)) {
      closing = true;
      flui_window_close(handle);
    }
    return;
  }
  if (kind == FLUI_EVENT_CLOSED) {
    closed = true;
    if (main)
      flui_app_quit();
    return;
  }
  if (kind != FLUI_EVENT_ACTION)
    return;
  if (name == "0:frames") {
    std::istringstream input(value);
    std::string frame;
    while (std::getline(input, frame, ';')) {
      auto v = detail::integers(frame);
      if (v.size() == 3 && frameTimings.size() < 4096)
        frameTimings.push_back({v[0], v[1], v[2]});
    }
    return;
  }
  if (name == "0:close" && root->Attribute("dialog") == "true") {
    if (!close || close(true)) {
      closing = true;
      flui_window_close(handle);
    }
    return;
  }
  const auto split = name.find(':');
  if (split == name.npos)
    return;
  uint64_t id = 0;
  try {
    id = std::stoull(name.substr(0, split));
  } catch (...) {
    return;
  }
  auto i = controls.find(id);
  if (i == controls.end())
    return;
  if (auto c = i->second.lock())
    if (c->owner_.lock().get() == this) {
      const auto event = name.substr(split + 1);
      if (event != "layout" && event != "viewport" && event != "pane") {
        for (auto ancestor = c; ancestor; ancestor = ancestor->parent_.lock())
          if (!ancestor->IsEnabled() || !ancestor->IsVisible())
            return;
      }
      c->Receive(event, value);
      if (event == "layout" && resized && !layoutScheduled) {
        layoutScheduled = true;
        std::weak_ptr<WindowState> weak = shared_from_this();
        detail::post([weak] {
          if (auto s = weak.lock()) {
            s->layoutScheduled = false;
            if (!s->closed && !s->closing && s->resized)
              s->resized();
          }
        });
      }
    }
}
class Timer {
  flui_timer handle_ = 0;
  std::function<void()> callback_;

public:
  ~Timer() { Stop(); }
  Timer() = default;
  Timer(const Timer &) = delete;
  Timer &operator=(const Timer &) = delete;
  void Start(uint32_t ms, std::function<void()> f) {
    Stop();
    callback_ = std::move(f);
    check(flui_timer_create(
        ms,
        [](void *u) {
          auto fn = static_cast<Timer *>(u)->callback_;
          try {
            if (fn)
              fn();
          } catch (const std::exception &e) {
            std::fprintf(stderr, "libflui timer: %s\n", e.what());
          }
        },
        this, &handle_));
  }
  void Stop() {
    if (handle_)
      flui_timer_destroy(std::exchange(handle_, 0));
    callback_ = {};
  }
};
struct DesktopWindowSpec {
  std::string title;
  Extent size{1000, 700}, minimumSize{640, 480};
  int statusFields = 0;
  bool standardMenu = false;
};
using UiPost = std::function<bool(std::function<void()>)>;
class DesktopWindow {
  std::shared_ptr<WindowState> state_;
  Extent size_;
  bool presented_ = false;
  static inline size_t count_ = 0;
  static inline std::vector<DesktopWindow *> all_;
  void Property(const char *key, const std::string &value) {
    check(flui_window_property(state_->handle, span(key), span(value)));
  }

public:
  explicit DesktopWindow(const DesktopWindowSpec &spec, const std::string &xml)
      : state_(std::make_shared<WindowState>()), size_(spec.size) {
    state_->root = CreateControlFromXml(xml);
    state_->root->SetAttribute("tooltips", tooltipsEnabled ? "true" : "false");
    state_->Attach(state_->root);
    windowStates.push_back(state_);
    flui_window_options options{
        sizeof(options),
        FLUI_ABI_VERSION,
        uint32_t(spec.size.width),
        uint32_t(spec.size.height),
        span(spec.title),
        [](const flui_event *event, void *u) {
          auto *s = static_cast<WindowState *>(u);
          std::weak_ptr<WindowState> weak = s->shared_from_this();
          auto kind = event->kind;
          auto status = event->status;
          auto name = copy(event->name), value = copy(event->value);
          detail::post([weak, kind, status, name = std::move(name),
                        value = std::move(value)] {
            if (auto s = weak.lock())
              s->Event(kind, status, name, value);
          });
        },
        state_.get()};
    check(flui_window_create(&options, &state_->handle));
    state_->main = count_++ == 0;
    all_.push_back(this);
    Property("managed_close", "true");
    Property("minimum",
             std::to_string(std::max(160, spec.minimumSize.width)) + "," +
                 std::to_string(std::max(120, spec.minimumSize.height)));
  }
  ~DesktopWindow() {
    all_.erase(std::remove(all_.begin(), all_.end(), this), all_.end());
    if (state_->handle) {
      state_->closed = true;
      flui_window_destroy(state_->handle);
      --count_;
    }
  }
  DesktopWindow(const DesktopWindow &) = delete;
  DesktopWindow &operator=(const DesktopWindow &) = delete;
  Control *FindControl(const std::string &n) const {
    return state_->root->FindControl(n);
  }
  template <class T> T *Require(const std::string &n) const {
    auto *c = dynamic_cast<T *>(FindControl(n));
    if (!c)
      throw std::runtime_error("Missing libflui control: " + n);
    return c;
  }
  static std::vector<DesktopWindow *> All() { return all_; }
  Control &Root() { return *state_->root; }
  void Present() {
    check(flui_window_show(state_->handle));
    presented_ = true;
  }
  void Raise() { Present(); }
  void SetTitle(const std::string &s) { Property("title", s); }
  void SetClientExtent(Extent s) {
    size_ = s;
    Property("size", std::to_string(s.width) + "," + std::to_string(s.height));
  }
  Extent ClientExtent() const {
    auto r = state_->root->GetRect();
    return r.width ? Extent{r.width, r.height} : size_;
  }
  void SetTheme(const std::string &name, const std::string &tokens = "{}") {
    state_->root->SetAttribute("theme", name);
    state_->root->SetAttribute("theme_tokens", tokens);
  }
  void SetFont(const std::string &f, double size) {
    state_->root->SetFont(f, size);
  }
  void RefreshLayout() { state_->Schedule(); }
  UiPost Poster() const {
    std::weak_ptr<WindowState> weak = state_;
    return [weak](std::function<void()> fn) {
      auto s = weak.lock();
      if (!s || s->closed)
        return false;
      detail::post([weak, fn = std::move(fn)] {
        if (auto s = weak.lock(); s && !s->closed)
          fn();
      });
      return true;
    };
  }
  void OnLayout(std::function<void()> fn) { state_->resized = std::move(fn); }
  void OnClose(std::function<bool(bool)> fn) { state_->close = std::move(fn); }
  void RequestClose() {
    if (!state_->closed)
      flui_window_request_close(state_->handle);
  }
  void FinishClose() {
    if (!state_->closed && !state_->closing) {
      state_->closing = true;
      flui_window_close(state_->handle);
    }
  }
  bool Ready() const { return state_->ready; }
  bool Idle() const { return !state_->inflight && !state_->scheduled; }
  std::string ErrorText() const { return state_->error; }
  void EnableFrameProfiling(bool enabled) {
    state_->root->SetAttribute("profile", enabled ? "true" : "false");
  }
  std::vector<FrameTiming> TakeFrameTimings() {
    return std::exchange(state_->frameTimings, {});
  }
  uint64_t PatchCount() const { return state_->patches; }
  uint64_t TransferredBytes() const { return state_->bytes; }
  std::optional<std::string> File(uint32_t kind, const std::string &title,
                                  const std::string &initial = {},
                                  const std::string &filter = {}) {
    std::optional<std::string> result;
    check(flui_file_dialog(
        kind, span(title), span(initial), span(filter),
        [](flui_string s, void *p) {
          *static_cast<std::optional<std::string> *>(p) = copy(s);
        },
        &result));
    return result;
  }
  static std::string Extensions(const std::string &filter) {
    std::string out;
    size_t at = 0;
    while ((at = filter.find("*.", at)) != std::string::npos) {
      at += 2;
      const auto end = filter.find_first_of(";| ", at);
      const auto ext =
          filter.substr(at, end == std::string::npos ? end : end - at);
      if (!ext.empty() && ext != "*") {
        if (!out.empty())
          out += ',';
        out += ext;
      }
      if (end == std::string::npos)
        break;
      at = end;
    }
    return out;
  }
  std::optional<std::string> OpenFile(const std::string &title,
                                      const std::string &filter = {}) {
    return File(FLUI_FILE_OPEN, title, {}, Extensions(filter));
  }
  std::optional<std::string> SaveFile(const std::string &title,
                                      const std::string &filename,
                                      const std::string &filter = {}) {
    return File(FLUI_FILE_SAVE, title, filename, Extensions(filter));
  }
  std::optional<std::string> ChooseDirectory(const std::string &title) {
    return File(FLUI_FILE_DIRECTORY, title);
  }
  bool ShowDialog(const std::string &title, const std::string &xml, Extent size,
                  std::function<void(Control &)> initialize = {}) {
    DesktopWindow dialog({title, size, {160, 120}, 0}, xml);
    dialog.state_->main = false;
    dialog.Root().SetAttribute("dialog", "true");
    bool accepted = false;
    if (auto *accept = dialog.FindControl("accept"))
      accept->Bind("click", [&](const auto &) {
        accepted = true;
        dialog.FinishClose();
      });
    if (auto *close = dialog.FindControl("close"))
      close->Bind("click", [&](const auto &) { dialog.FinishClose(); });
    if (initialize)
      initialize(dialog.Root());
    dialog.Present();
    check(flui_window_run_modal(dialog.state_->handle));
    return accepted;
  }
  void ShowText(const std::string &title, std::string_view text) {
    ShowDialog(
        title,
        R"(<Window><VerticalLayout inset="18,16,18,16" childpadding="12"><List name="body" showheader="false"/><HorizontalLayout height="30"><Control/><Button name="close" text="关闭" width="70"/></HorizontalLayout></VerticalLayout></Window>)",
        {700, 500}, [&](Control &root) {
          auto *list = dynamic_cast<List *>(root.FindControl("body"));
          auto row = std::make_shared<ListContainerElement>();
          auto label = std::make_shared<Label>();
          label->SetText(std::string(text));
          label->SetAttribute("wordwrap", "true");
          label->SetAttribute("valign", "top");
          row->SetFixedHeight(std::max(40, label->MeasureText(650)));
          row->Add(label);
          list->Add(row);
        });
  }
  void Error(const std::string &title, const std::string &text) {
    ShowText(title, text);
  }
  bool Confirm(const std::string &title, const std::string &text) {
    return ShowDialog(
        title,
        R"(<Window><VerticalLayout inset="20,20,20,20"><Label name="message" wordwrap="true"/><HorizontalLayout height="32"><Control/><Button name="close" text="取消" width="80"/><Button name="accept" text="确定" width="80"/></HorizontalLayout></VerticalLayout></Window>)",
        {480, 260},
        [&](Control &root) { root.FindControl("message")->SetText(text); });
  }
};
inline std::string ExecutablePath() {
  std::string out;
  check(flui_executable_path(
      [](flui_string s, void *p) { *static_cast<std::string *>(p) = copy(s); },
      &out));
  return out;
}
inline bool WriteFileAtomically(const std::string &path,
                                const std::string &data) {
  return flui_write_file_atomic(span(path), span(data)) == FLUI_OK;
}
inline void Bell() { flui_bell(); }
inline void EnableTooltips(bool b) {
  tooltipsEnabled = b;
  for (auto i = windowStates.begin(); i != windowStates.end();)
    if (auto s = i->lock()) {
      if (!s->closed)
        s->root->SetAttribute("tooltips", b ? "true" : "false");
      ++i;
    } else
      i = windowStates.erase(i);
}
inline bool ContainsIgnoringCase(std::string text, std::string query) {
  auto lower = [](unsigned char c) {
    return char(c < 128 ? std::tolower(c) : c);
  };
  std::transform(text.begin(), text.end(), text.begin(), lower);
  std::transform(query.begin(), query.end(), query.begin(), lower);
  return text.find(query) != text.npos;
}
inline auto UtcNow() { return std::chrono::system_clock::now(); }
inline std::string FormatUtc(std::chrono::system_clock::time_point point) {
  const auto t = std::chrono::system_clock::to_time_t(point);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char out[32];
  std::strftime(out, sizeof(out), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return out;
}
inline std::optional<std::chrono::system_clock::time_point>
ParseUtc(std::string s) {
  std::tm tm{};
  std::istringstream in(s);
  in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  if (in.fail() || in.peek() != std::char_traits<char>::eof())
    return {};
  auto t = timegm(&tm);
  if (t < 0 || FormatUtc(std::chrono::system_clock::from_time_t(t)) != s)
    return {};
  return std::chrono::system_clock::from_time_t(t);
}
inline std::string FormatLocalTime(std::string s) {
  auto normalized = s;
  if (auto dot = normalized.find('.'); dot != normalized.npos)
    normalized = normalized.substr(0, dot) + "Z";
  auto t = ParseUtc(normalized);
  if (!t)
    return s;
  auto stamp = std::chrono::system_clock::to_time_t(*t);
  std::tm tm{};
  localtime_r(&stamp, &tm);
  char out[40];
  std::strftime(out, sizeof(out), "%Y-%m-%d %H:%M:%S", &tm);
  return out;
}
class Application {
  std::vector<std::string> arguments_;

protected:
  virtual bool OnAppInit() = 0;
  virtual int OnAppExit() { return 0; }

public:
  virtual ~Application() = default;
  const auto &Arguments() const { return arguments_; }
  int Run(int argc, char **argv) {
    for (int i = 0; i < argc; ++i)
      arguments_.emplace_back(argv[i]);
    if (!OnAppInit())
      return 1;
    const auto result = flui_app_run();
    const int exit = OnAppExit();
    return result == FLUI_OK ? exit : 1;
  }
};
} // namespace flui
#define FLUI_IMPLEMENT_APPLICATION(T)                                          \
  int main(int argc, char **argv) {                                            \
    T app;                                                                     \
    return app.Run(argc, argv);                                                \
  }
