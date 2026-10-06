#pragma once
/// libwxui — Window root descriptor.
/// Describes top-level window properties; rendered by UIManager.

#include "container.hpp"

namespace wxui {

class Window : public Container {
public:
    void SetAttribute(const std::string& key, const std::string& val) override;
    void DoLayout(const wxRect& rc) override;
    std::string GetTag() const override { return "Window"; }

    // ── Window-level geometry ─────────────────────────────────────────
    [[nodiscard]] wxSize  GetInitSize()    const { return initSize_; }
    [[nodiscard]] wxRect  GetSizeBox()     const { return sizeBox_; }
    [[nodiscard]] wxRect  GetCaption()     const { return caption_; }
    [[nodiscard]] wxSize  GetRoundCorner() const { return roundCorner_; }
    [[nodiscard]] wxSize  GetMinInfo()     const { return minInfo_; }
    [[nodiscard]] wxSize  GetMaxInfo()     const { return maxInfo_; }
    [[nodiscard]] uint8_t GetAlpha()       const { return alpha_; }
    [[nodiscard]] bool    IsBkTrans()      const { return bkTrans_; }

    // ── Global colour defaults ────────────────────────────────────────
    [[nodiscard]] wxColour GetDisabledFontColor()   const { return disabledFontColor_; }
    [[nodiscard]] wxColour GetDefaultFontColor()    const { return defaultFontColor_; }
    [[nodiscard]] wxColour GetLinkFontColor()       const { return linkFontColor_; }
    [[nodiscard]] wxColour GetLinkHoverFontColor()  const { return linkHoverFontColor_; }
    [[nodiscard]] wxColour GetSelectedColor()       const { return selectedColor_; }

    [[nodiscard]] bool     IsShowDirty()            const { return showDirty_; }

private:
    // window properties
    wxSize   initSize_           {0, 0};
    wxRect   sizeBox_            {0, 0, 0, 0};
    wxRect   caption_            {0, 0, 0, 0};
    wxSize   roundCorner_        {0, 0};
    wxSize   minInfo_            {0, 0};
    wxSize   maxInfo_            {0, 0};
    uint8_t  alpha_              = 255;
    bool     bkTrans_            = false;
    bool     showDirty_          = false;

    // global colour defaults
    wxColour disabledFontColor_  {0xA7, 0xA6, 0xAA, 0xFF};
    wxColour defaultFontColor_   {0x00, 0x00, 0x00, 0xFF};
    wxColour linkFontColor_      {0x00, 0x00, 0xFF, 0xFF};
    wxColour linkHoverFontColor_ {0xD3, 0x21, 0x5F, 0xFF};
    wxColour selectedColor_      {0xBA, 0xE4, 0xFF, 0xFF};
};

} // namespace wxui
