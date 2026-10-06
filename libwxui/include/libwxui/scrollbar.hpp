#pragma once
/// libwxui — ScrollBar control (fully custom-drawn).

#include "control.hpp"

namespace wxui {

class ScrollBar : public Control {
public:
    void DoPaint(wxDC& dc, const wxRect& clipRect) override;
    void SetAttribute(const std::string& key, const std::string& val) override;
    std::string GetTag() const override { return "ScrollBar"; }

    void OnMouseMove(const wxPoint& pt)  override;
    void OnButtonDown(const wxPoint& pt) override;
    void OnButtonUp(const wxPoint& pt)   override;
    void OnMouseLeave()                  override;

    // ── Value interface ────────────────────────────────────────────────
    void SetRange(int r)    { range_ = std::max(0, r); ClampValue(); Invalidate(); }
    void SetValue(int v);
    void SetLineSize(int l) { lineSize_ = std::max(1, l); }
    void SetPageSize(int p) { pageSize_ = std::max(1, p); }

    [[nodiscard]] int  GetRange()    const { return range_; }
    [[nodiscard]] int  GetValue()    const { return value_; }
    [[nodiscard]] int  GetLineSize() const { return lineSize_; }
    [[nodiscard]] bool IsHorizontal()const { return hor_; }

private:
    enum class Part { None, Btn1, Track, Thumb, Btn2 };
    Part    HitTest(const wxPoint& pt)  const;
    wxRect  GetThumbRect()              const;
    wxRect  GetTrackRect()              const;
    wxRect  GetBtn1Rect()               const;
    wxRect  GetBtn2Rect()               const;
    void    ClampValue();

    // images: index 0=Normal 1=Hot 2=Pushed 3=Disabled
    ImageSpec btn1Images_[4];
    ImageSpec btn2Images_[4];
    ImageSpec thumbImages_[4];
    ImageSpec railImages_[4];
    ImageSpec bkImages_[4];

    bool hor_         = false;
    int  btnSize_     = 16;   ///< pixel size of the arrow buttons
    int  lineSize_    = 8;
    int  pageSize_    = 100;
    int  range_       = 100;
    int  value_       = 0;
    bool showButton1_ = true;
    bool showButton2_ = true;

    Part     pressedPart_   = Part::None;
    bool     draggingThumb_ = false;
    bool     thumbHot_      = false;
    int      dragThumbOffset_ = 0;
};

} // namespace wxui
