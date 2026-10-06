#pragma once
/// libwxui — GifAnim control (animated GIF playback via wxAnimation).

#include "control.hpp"
#include <wx/animate.h>
#include <wx/timer.h>

namespace wxui {

class GifAnim : public Control {
public:
    ~GifAnim() override;
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "GifAnim"; }

    void OnManagerSet() override;

    void Play();
    void Stop();
    void Reset();
    [[nodiscard]] bool IsPlaying() const { return playing_; }

private:
    void LoadGif();
    void OnTimer(wxTimerEvent&);

    wxAnimation anim_;
    wxBitmap    currentFrame_;
    int         frameIdx_  = 0;
    bool        autoPlay_  = true;
    bool        autoSize_  = false;
    bool        playing_   = false;
    std::string gifPath_;
    wxTimer*    timer_     = nullptr;   ///< owned; deleted in destructor
};

} // namespace wxui
