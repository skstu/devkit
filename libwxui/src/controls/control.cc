#include <libwxui.hpp>
#include "../graphics.hpp"

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/log.h>
#include <algorithm>
#include <cmath>
#include <memory>

namespace wxui {
namespace {

double RoundedRadius(const wxRect& rect, const wxSize& round) {
    const int requested = std::max(round.x, round.y);
    if (requested <= 0 || rect.width <= 0 || rect.height <= 0) return 0.0;
    return std::min<double>(requested,
                            std::min(rect.width, rect.height) / 2.0);
}

bool FillRoundedRect(wxDC& dc, const wxRect& rect, const wxColour& color,
                     const wxSize& round) {
    const double radius = RoundedRadius(rect, round);
    if (radius <= 0.0) return false;

    GraphicsScope gc(dc);
    if (!gc) return false;
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(color));
    gc->DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);
    return true;
}

bool StrokeRoundedRect(wxDC& dc, const wxRect& rect, const wxColour& color,
                       int width, const wxSize& round) {
    if (width <= 0) return false;

    const double inset = width / 2.0;
    const double x = rect.x + inset;
    const double y = rect.y + inset;
    const double w = std::max(0.0, static_cast<double>(rect.width - width));
    const double h = std::max(0.0, static_cast<double>(rect.height - width));
    if (w <= 0.0 || h <= 0.0) return false;

    const double radius = std::max(0.0, RoundedRadius(rect, round) - inset);

    GraphicsScope gc(dc);
    if (!gc) return false;
    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(color, width));
    gc->DrawRoundedRectangle(x, y, w, h, radius);
    return true;
}

} // namespace

// ── SetAttribute ─────────────────────────────────────────────────────────
void Control::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "windowdrag") { windowDrag_ = ParseBOOL(val); return; }
    if (key == "bevel") { const bool next = ParseBOOL(val); if (bevel_ != next) { bevel_ = next; Invalidate(); } return; }
    if (key == "name")            { name_      = val;                       return; }
    if (key == "tooltip")         { tooltip_ = val; if (manager_) manager_->UpdateTooltip(); return; }
    if (key == "userdata" || key == "userData") { userData_ = val;          return; }
    if (key == "text")            { SetText(val);                             return; }
    if (key == "shortcut")        { shortcut_  = val.empty() ? '\0' : val[0]; return; }
    if (key == "menu")            { menu_      = ParseBOOL(val);            return; }
    if (key == "keyboard")        { keyboard_  = ParseBOOL(val);            return; }

    if (key == "pos")             { rect_ = ParseRECT(val, rect_);          return; }
    if (key == "padding")         { padding_ = ParseRECT(val);              return; }
    if (key == "width")           { fixedSize_.SetWidth(ParseINT(val));     return; }
    if (key == "height")          { fixedSize_.SetHeight(ParseINT(val));    return; }
    if (key == "weight")          { layoutWeight_ = std::max(1, ParseINT(val, 1)); return; }
    if (key == "minwidth")        { minSize_.SetWidth(ParseINT(val));       return; }
    if (key == "minheight")       { minSize_.SetHeight(ParseINT(val));      return; }
    if (key == "maxwidth")        { maxSize_.SetWidth(ParseINT(val));       return; }
    if (key == "maxheight")       { maxSize_.SetHeight(ParseINT(val));      return; }

    const auto color = [&](wxColour& current) { const auto next=ParseDWORD(val); if(current!=next){current=next;Invalidate();} };
    if (key == "bkcolor")         { color(bkColor_); return; }
    if (key == "bkcolor2")        { color(bkColor2_); return; }
    if (key == "bkcolor3")        { color(bkColor3_); return; }
    if (key == "gradientend") { double value = 1; if (Utf8ToWxString(val).ToDouble(&value) && std::isfinite(value)) { value = std::clamp(value, 0.001, 1.0); if (gradientEnd_ != value) { gradientEnd_ = value; Invalidate(); } } return; }
    if (key == "bordercolor")     { color(borderColor_); return; }
    if (key == "cornerradius")    { cornerRadius_ = ParseRECT(val); customCorners_ = true; return; }
    if (key == "focusbordercolor")
                                  { focusBorderColor_ = ParseDWORD(val);    return; }
    if (key == "borderround")     {
        if (val.find(',') != std::string::npos) {
            borderRound_ = ParseSIZE(val, borderRound_);
        } else {
            const int r = ParseINT(val);
            borderRound_ = wxSize(r, r);
        }
        return;
    }
    if (key == "bordersize")      {
        if (val.find(',') != std::string::npos) {
            const wxRect border = ParseRECT(val);
            leftBorder_ = std::max(0, border.x);
            topBorder_ = std::max(0, border.y);
            rightBorder_ = std::max(0, border.width);
            bottomBorder_ = std::max(0, border.height);
            borderSize_ = std::max(std::max(leftBorder_, topBorder_),
                                   std::max(rightBorder_, bottomBorder_));
        } else {
            borderSize_ = std::max(0, ParseINT(val));
        }
        return;
    }
    if (key == "leftbordersize")  { leftBorder_   = std::max(0, ParseINT(val)); return; }
    if (key == "topbordersize")   { const int next=std::max(0,ParseINT(val)); if(topBorder_!=next){topBorder_=next;Invalidate();} return; }
    if (key == "rightbordersize") { rightBorder_  = std::max(0, ParseINT(val)); return; }
    if (key == "bottombordersize") { bottomBorder_ = std::max(0, ParseINT(val)); return; }
    if (key == "borderstyle")     { borderStyle_  = ParseINT(val);           return; }

    if (key == "leftborder")      { leftBorder_   = ParseBOOL(val) ? std::max(1, borderSize_) : 0; return; }
    if (key == "topborder")       { topBorder_    = ParseBOOL(val) ? std::max(1, borderSize_) : 0; return; }
    if (key == "rightborder")     { rightBorder_  = ParseBOOL(val) ? std::max(1, borderSize_) : 0; return; }
    if (key == "bottomborder")    { bottomBorder_ = ParseBOOL(val) ? std::max(1, borderSize_) : 0; return; }

    if (key == "bkimage")         { bkImage_  = ParseImageSpec(val);        return; }

    if (key == "enabled")         { SetEnabled(ParseBOOL(val, true));       return; }
    if (key == "mouse")           { mouse_    = ParseBOOL(val, true);       return; }
    if (key == "visible")         { SetVisible(ParseBOOL(val, true));       return; }
    if (key == "float")           { float_    = ParseBOOL(val, false);      return; }
    if (key == "colorhsl")        { colorHsl_ = ParseBOOL(val, false);      return; }

    wxLogVerbose("libwxui: %s ignores attr '%s'",
                 GetTag().c_str(), key.c_str());
}

std::string Control::GetAttribute(const std::string& key) const {
    if (key == "name") return name_;
    if (key == "tooltip") return tooltip_;
    if (key == "userdata" || key == "userData") return userData_;
    if (key == "text") return text_;
    return {};
}

void Control::SetRect(const wxRect& rc) {
    rect_ = rc;
}

bool Control::IsEffectivelyVisible() const {
    const Control* current = this;
    while (current) {
        if (!current->visible_) return false;
        current = current->parent_;
    }
    return true;
}

// ── DoPaint ───────────────────────────────────────────────────────────────
bool Control::IsNativeWindowVisible() const {
    if (rect_.width <= 0 || rect_.height <= 0) return false;

    wxRect visibleRect = rect_;
    const Control* current = this;
    while (current) {
        if (!current->visible_) return false;
        visibleRect = visibleRect.Intersect(current->rect_);
        if (visibleRect.IsEmpty()) return false;
        current = current->parent_;
    }

    if (manager_) {
        visibleRect = visibleRect.Intersect(manager_->GetClientRect());
        if (visibleRect.IsEmpty()) return false;
    }

    return visibleRect.x == rect_.x &&
           visibleRect.y == rect_.y &&
           visibleRect.width == rect_.width &&
           visibleRect.height == rect_.height;
}

void Control::DoPaint(wxDC& dc, const wxRect& /*clipRect*/) {
    DrawBkColor(dc);
    DrawBkImage(dc);
    DrawBorder(dc);
}

void Control::DrawBkColor(wxDC& dc) {
    if (!bkColor_.IsOk() || bkColor_.Alpha() == 0) return;
    if (customCorners_ || bkColor2_.IsOk()) {
        GraphicsScope gc(dc);
        if (gc) {
            const double x = rect_.x, y = rect_.y, w = rect_.width, h = rect_.height;
            const auto radius = [&](int r) { return std::clamp(double(r), 0.0, std::max(0.0, std::min(w, h) / 2)); };
            const double tl = radius(customCorners_ ? cornerRadius_.x : borderRound_.x);
            const double tr = radius(customCorners_ ? cornerRadius_.y : borderRound_.x);
            const double br = radius(customCorners_ ? cornerRadius_.width : borderRound_.x);
            const double bl = radius(customCorners_ ? cornerRadius_.height : borderRound_.x);
            auto path = gc->CreatePath();
            path.MoveToPoint(x + tl, y);
            path.AddLineToPoint(x + w - tr, y);
            path.AddQuadCurveToPoint(x + w, y, x + w, y + tr);
            path.AddLineToPoint(x + w, y + h - br);
            path.AddQuadCurveToPoint(x + w, y + h, x + w - br, y + h);
            path.AddLineToPoint(x + bl, y + h);
            path.AddQuadCurveToPoint(x, y + h, x, y + h - bl);
            path.AddLineToPoint(x, y + tl);
            path.AddQuadCurveToPoint(x, y, x + tl, y);
            path.CloseSubpath();
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(bkColor2_.IsOk() ? gc->CreateLinearGradientBrush(x, y, x, y + h * gradientEnd_, bkColor_, bkColor2_) : gc->CreateBrush(wxBrush(bkColor_)));
            gc->FillPath(path);
            return;
        }
        if (bkColor2_.IsOk()) {
            const int gradientHeight = std::max(1, int(rect_.height * gradientEnd_));
            dc.GradientFillLinear({rect_.x, rect_.y, rect_.width, gradientHeight}, bkColor_, bkColor2_, wxSOUTH);
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(bkColor2_));
            dc.DrawRectangle(rect_.x, rect_.y + gradientHeight, rect_.width, rect_.height - gradientHeight);
            return;
        }
    }
    if (FillRoundedRect(dc, rect_, bkColor_, borderRound_)) return;

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(bkColor_));
    dc.DrawRectangle(rect_);
}

void Control::DrawBkImage(wxDC& dc) {
    if (bkImage_.IsEmpty() || !manager_) return;
    wxBitmap bmp = manager_->LoadBitmap(bkImage_.path);
    if (!bmp.IsOk()) return;

    const wxRect& dest = bkImage_.dest.IsEmpty() ? rect_ : bkImage_.dest;

    if (!bkImage_.corner.IsEmpty()) {
        // 9-patch rendering
        const wxRect& cor = bkImage_.corner;
        const wxSize  srcSz(bmp.GetWidth(), bmp.GetHeight());
        // Simplified: draw scaled for now; full 9-patch can be added per need
        dc.DrawBitmap(bmp.GetSubBitmap(wxRect(wxPoint(0,0), srcSz)),
                      dest.GetTopLeft());
    } else {
        wxImage img = bmp.ConvertToImage().Scale(dest.GetWidth(), dest.GetHeight());
        dc.DrawBitmap(wxBitmap(img), dest.GetTopLeft());
    }
}

void Control::DrawBorder(wxDC& dc) {
    if (bevel_ && rect_.width > 6 && rect_.height > 6) {
        auto r = rect_;
        r.Deflate(1);
        dc.SetPen(wxPen(wxColour(248, 251, 253), 1));
        dc.DrawLine(r.x + 2, r.y, r.GetRight() - 1, r.y);
        dc.DrawLine(r.x, r.y + 2, r.x, r.GetBottom() - 1);
        dc.SetPen(wxPen(wxColour(112, 128, 139), 1));
        dc.DrawLine(r.x + 2, r.GetBottom(), r.GetRight() - 1, r.GetBottom());
    }
    if (!borderColor_.IsOk()) return;

    const bool hasExplicitSides = leftBorder_ > 0 || topBorder_ > 0 ||
                                  rightBorder_ > 0 || bottomBorder_ > 0;
    const int left = hasExplicitSides ? leftBorder_ : borderSize_;
    const int top = hasExplicitSides ? topBorder_ : borderSize_;
    const int right = hasExplicitSides ? rightBorder_ : borderSize_;
    const int bottom = hasExplicitSides ? bottomBorder_ : borderSize_;

    if (left <= 0 && top <= 0 && right <= 0 && bottom <= 0) return;

    if (!hasExplicitSides && borderRound_.x > 0 &&
        left == top && top == right && right == bottom) {
        if (!StrokeRoundedRect(dc, rect_, borderColor_, left, borderRound_)) {
            wxPen pen(borderColor_, left);
            dc.SetPen(pen);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            const int half = left / 2;
            wxRect rounded = rect_;
            rounded.Deflate(half);
            dc.DrawRoundedRectangle(rounded, borderRound_.x);
        }
        return;
    }

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(borderColor_));
    if (top > 0) {
        dc.DrawRectangle(rect_.x, rect_.y, rect_.width, top);
    }
    if (bottom > 0) {
        dc.DrawRectangle(rect_.x, rect_.y + rect_.height - bottom,
                         rect_.width, bottom);
    }
    if (left > 0) {
        dc.DrawRectangle(rect_.x, rect_.y + top, left,
                         std::max(0, rect_.height - top - bottom));
    }
    if (right > 0) {
        dc.DrawRectangle(rect_.x + rect_.width - right, rect_.y + top,
                         right, std::max(0, rect_.height - top - bottom));
    }
}

// ── Invalidate ────────────────────────────────────────────────────────────
void Control::Invalidate() {
    if (manager_) manager_->InvalidateControl(this);
}

// ── FindControl ───────────────────────────────────────────────────────────
Control* Control::FindControl(const std::string& name) {
    if (name_ == name) return this;
    return nullptr;
}

// ── FireNotify ────────────────────────────────────────────────────────────
void Control::FireNotify(const std::string& notify,
                         int x, int y, intptr_t p1, const std::string& sp) {
    NotifyEvent e;
    e.sender   = this;
    e.notify   = notify;
    e.x        = x;
    e.y        = y;
    e.param1   = p1;
    e.strParam = sp;
    events_.Fire(e);
}

// ── SetVisible / SetEnabled ───────────────────────────────────────────────
void Control::SetText(const std::string& t) {
    if (text_ == t) return;
    text_ = t;
    Invalidate();
}

void Control::SetText(const char* t) {
    SetText(std::string(t ? t : ""));
}

void Control::SetText(const wxString& t) {
    SetText(WxStringToUtf8(t));
}

void Control::SetVisible(bool v) {
    if (visible_ == v) return;
    visible_ = v;
    Invalidate();
}

void Control::SetEnabled(bool e) {
    if (enabled_ == e) return;
    enabled_ = e;
    Invalidate();
}

} // namespace wxui
