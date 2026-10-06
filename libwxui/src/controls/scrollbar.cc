#include <libwxui.hpp>
#include <libwxui/appearance.hpp>

#include <wx/log.h>
#include <algorithm>

namespace wxui {

// ── SetAttribute ─────────────────────────────────────────────────────────
void ScrollBar::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "hor")      { hor_         = ParseBOOL(val);    return; }
    if (key == "linesize" ||
        key == "lineSize") { lineSize_    = ParseINT(val, 1);  return; }
    if (key == "range")    { range_       = ParseINT(val, 100);return; }
    if (key == "value")    { SetValue(ParseINT(val));           return; }
    if (key == "showbutton1") { showButton1_ = ParseBOOL(val, true); return; }
    if (key == "showbutton2") { showButton2_ = ParseBOOL(val, true); return; }

    // Image arrays  —  naming convention:  btn1normalimage / btn1hotimage …
    auto tryImg = [&](const char* prefix, ImageSpec (&arr)[4]) -> bool {
        for (int i = 0; i < 4; ++i) {
            static const char* suffixes[4] = {
                "normalimage","hotimage","pushedimage","disabledimage"
            };
            if (key == std::string(prefix) + suffixes[i]) {
                arr[i] = ParseImageSpec(val);
                return true;
            }
        }
        return false;
    };

    if (tryImg("btn1",  btn1Images_))   return;
    if (tryImg("btn2",  btn2Images_))   return;
    if (tryImg("thumb", thumbImages_))  return;
    if (tryImg("rail",  railImages_))   return;
    if (tryImg("bk",    bkImages_))     return;

    Control::SetAttribute(key, val);
}

// ── Geometry helpers ──────────────────────────────────────────────────────
wxRect ScrollBar::GetBtn1Rect() const {
    if (!showButton1_) return wxRect{};
    if (hor_) return wxRect(rect_.x, rect_.y, btnSize_, rect_.GetHeight());
    return        wxRect(rect_.x, rect_.y, rect_.GetWidth(), btnSize_);
}

wxRect ScrollBar::GetBtn2Rect() const {
    if (!showButton2_) return wxRect{};
    if (hor_) return wxRect(rect_.GetRight() - btnSize_ + 1, rect_.y,
                            btnSize_, rect_.GetHeight());
    return wxRect(rect_.x, rect_.GetBottom() - btnSize_ + 1,
                  rect_.GetWidth(), btnSize_);
}

wxRect ScrollBar::GetTrackRect() const {
    wxRect track = rect_;
    if (hor_) {
        if (showButton1_) track.x      += btnSize_;
        track.width = std::max(0, track.width - btnSize_ * (int(showButton1_) + int(showButton2_)));
    } else {
        if (showButton1_) track.y      += btnSize_;
        track.height = std::max(0, track.height - btnSize_ * (int(showButton1_) + int(showButton2_)));
    }
    return track;
}

wxRect ScrollBar::GetThumbRect() const {
    if (range_ <= pageSize_) return wxRect{};
    wxRect track = GetTrackRect();
    if (hor_) {
        const int trackW = track.GetWidth();
        const int thumbW = std::clamp(trackW * pageSize_ / range_, std::min(20, trackW), trackW);
        const int maxOff = trackW - thumbW;
        const int off    = (maxOff > 0) ? (value_ * maxOff / (range_ - pageSize_)) : 0;
        return wxRect(track.x + off, track.y, thumbW, track.GetHeight());
    } else {
        const int trackH = track.GetHeight();
        const int thumbH = std::clamp(trackH * pageSize_ / range_, std::min(20, trackH), trackH);
        const int maxOff = trackH - thumbH;
        const int off    = (maxOff > 0) ? (value_ * maxOff / (range_ - pageSize_)) : 0;
        return wxRect(track.x, track.y + off, track.GetWidth(), thumbH);
    }
}

ScrollBar::Part ScrollBar::HitTest(const wxPoint& pt) const {
    if (GetThumbRect().Contains(pt))   return Part::Thumb;
    if (GetBtn1Rect().Contains(pt))    return Part::Btn1;
    if (GetBtn2Rect().Contains(pt))    return Part::Btn2;
    if (GetTrackRect().Contains(pt))   return Part::Track;
    return Part::None;
}

// ── SetValue ───────────────────────────────────────────────────────────────
void ScrollBar::SetValue(int pos) {
    const int maxVal = std::max(0, range_ - pageSize_);
    value_ = std::clamp(pos, 0, maxVal);
    Invalidate();
}

void ScrollBar::ClampValue() {
    SetValue(value_);
}

// ── DoPaint ───────────────────────────────────────────────────────────────
static void DrawStateImage(wxDC& dc, const wxRect& r,
                            const ImageSpec* imgs, int stateIdx,
                            UIManager* mgr) {
    if (!imgs || !mgr) return;
    const ImageSpec& spec = imgs[stateIdx];
    if (spec.IsEmpty()) return;
    wxBitmap bmp = mgr->LoadBitmap(spec.path);
    if (!bmp.IsOk()) return;
    wxImage img = bmp.ConvertToImage().Scale(r.GetWidth(), r.GetHeight());
    dc.DrawBitmap(wxBitmap(img), r.GetTopLeft());
}

void ScrollBar::DoPaint(wxDC& dc, const wxRect& /*clipRect*/) {
    auto thumb = GetThumbRect();
    if (hor_) thumb.Deflate(0, 3); else thumb.Deflate(3, 0);
    PaintScrollChrome(dc, rect_, thumb, draggingThumb_ || thumbHot_,
        manager_ && manager_->GetRoot() ? manager_->GetRoot()->GetBkColor() : bkColor_);
    // Background
    DrawStateImage(dc, rect_, bkImages_, 0, manager_);

    // Track / rail
    DrawStateImage(dc, GetTrackRect(), railImages_, 0, manager_);

    // Buttons
    DrawStateImage(dc, GetBtn1Rect(), btn1Images_, 0, manager_);
    DrawStateImage(dc, GetBtn2Rect(), btn2Images_, 0, manager_);

    // Thumb
    const int ts = draggingThumb_ ? 2 : (thumbHot_ ? 1 : 0);
    DrawStateImage(dc, GetThumbRect(), thumbImages_, ts, manager_);
}

// ── Mouse ─────────────────────────────────────────────────────────────────
void ScrollBar::OnButtonDown(const wxPoint& pt) {
    Part hit = HitTest(pt);
    pressedPart_ = hit;
    switch (hit) {
    case Part::Btn1:  SetValue(value_ - lineSize_); FireNotify("scroll"); break;
    case Part::Btn2:  SetValue(value_ + lineSize_); FireNotify("scroll"); break;
    case Part::Track: {
        wxRect thumb = GetThumbRect();
        if (hor_) SetValue(pt.x < thumb.x ? value_ - pageSize_ : value_ + pageSize_);
        else      SetValue(pt.y < thumb.y ? value_ - pageSize_ : value_ + pageSize_);
        FireNotify("scroll");
        break;
    }
    case Part::Thumb:
        draggingThumb_ = true;
        dragThumbOffset_ = hor_ ? (pt.x - GetThumbRect().x)
                                : (pt.y - GetThumbRect().y);
        thumbHot_ = true;
        Invalidate();
        break;
    default: break;
    }
}

void ScrollBar::OnButtonUp(const wxPoint&) {
    draggingThumb_ = false;
    pressedPart_ = Part::None;
    Invalidate();
}

void ScrollBar::OnMouseMove(const wxPoint& pt) {
    thumbHot_ = draggingThumb_ || HitTest(pt) == Part::Thumb;
    if (draggingThumb_) {
        const wxRect track = GetTrackRect();
        const wxRect thumb = GetThumbRect();
        const int trackStart = hor_ ? track.x : track.y;
        const int trackSize = hor_ ? track.GetWidth() : track.GetHeight();
        const int thumbSize = hor_ ? thumb.GetWidth() : thumb.GetHeight();
        const int maxOff = std::max(0, trackSize - thumbSize);
        const int maxVal = std::max(0, range_ - pageSize_);

        if (maxOff > 0 && maxVal > 0) {
            const int mousePos = hor_ ? pt.x : pt.y;
            const int thumbOff = std::clamp(mousePos - dragThumbOffset_ -
                                                trackStart,
                                            0, maxOff);
            const int newVal = thumbOff * maxVal / maxOff;
            SetValue(newVal);
            FireNotify("scroll");
            if (manager_) manager_->Update();
        }
    }
}

void ScrollBar::OnMouseLeave() {
    thumbHot_ = false;
    Invalidate();
}

} // namespace wxui
