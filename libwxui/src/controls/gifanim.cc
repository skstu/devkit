#include <libwxui.hpp>

#include <wx/log.h>

namespace wxui {

GifAnim::~GifAnim() {
    Stop();
    delete timer_;
}

void GifAnim::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "autoplay"  || key == "autoPlay") { autoPlay_ = ParseBOOL(val, true);  return; }
    if (key == "autosize"  || key == "autoSize") { autoSize_ = ParseBOOL(val, false); return; }
    if (key == "gif" || key == "bkimage" || key == "src") {
        gifPath_ = val;
        return;
    }
    Control::SetAttribute(key, val);
}

void GifAnim::OnManagerSet() {
    LoadGif();
    if (autoPlay_) Play();
}

void GifAnim::LoadGif() {
    if (gifPath_.empty() || !manager_) return;
    const std::string full = manager_->GetResourceRoot().empty()
                           ? gifPath_
                           : manager_->GetResourceRoot() + "/" + gifPath_;
    if (!anim_.LoadFile(Utf8ToWxString(full)))
        wxLogWarning("libwxui GifAnim: failed to load '%s'", full.c_str());
}

void GifAnim::Play() {
    if (!anim_.IsOk() || playing_) return;
    playing_  = true;
    frameIdx_ = 0;
    if (!timer_) {
        timer_ = new wxTimer(manager_);
        manager_->Bind(wxEVT_TIMER, &GifAnim::OnTimer, this, timer_->GetId());
    }
    const int delay = anim_.GetDelay(0);
    timer_->Start(delay > 0 ? delay : 100);
}

void GifAnim::Stop() {
    if (!playing_) return;
    playing_ = false;
    if (timer_) timer_->Stop();
}

void GifAnim::Reset() {
    Stop();
    frameIdx_ = 0;
    Invalidate();
}

void GifAnim::OnTimer(wxTimerEvent&) {
    if (!anim_.IsOk() || !playing_) return;

    const int frameCount = anim_.GetFrameCount();
    if (frameCount <= 0) { Stop(); return; }

    frameIdx_ = (frameIdx_ + 1) % frameCount;
    currentFrame_ = anim_.GetFrame(frameIdx_);

    // Update timer delay for next frame
    const int delay = anim_.GetDelay(frameIdx_);
    if (timer_) timer_->Start(delay > 0 ? delay : 100, wxTIMER_ONE_SHOT);

    Invalidate();
}

void GifAnim::DoPaint(wxDC& dc, const wxRect& clipRect) {
    Control::DoPaint(dc, clipRect);
    if (!anim_.IsOk()) return;

    wxBitmap frame = currentFrame_.IsOk()
                   ? currentFrame_
                   : anim_.GetFrame(0);
    if (!frame.IsOk()) return;

    if (autoSize_) {
        dc.DrawBitmap(frame, rect_.GetTopLeft());
    } else {
        wxImage img = frame.ConvertToImage()
                           .Scale(rect_.GetWidth(), rect_.GetHeight());
        dc.DrawBitmap(wxBitmap(img), rect_.GetTopLeft());
    }
}

} // namespace wxui
