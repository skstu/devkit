#include <libwxui.hpp>

namespace wxui {

void Window::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "size")          { initSize_   = ParseSIZE(val);         return; }
    if (key == "sizebox")       { sizeBox_    = ParseRECT(val);         return; }
    if (key == "caption")       { caption_    = ParseRECT(val);         return; }
    if (key == "roundcorner")   { roundCorner_= ParseSIZE(val);         return; }
    if (key == "mininfo")       { minInfo_    = ParseSIZE(val);         return; }
    if (key == "maxinfo")       { maxInfo_    = ParseSIZE(val);         return; }
    if (key == "alpha")         { alpha_      = static_cast<uint8_t>(ParseINT(val)); return; }
    if (key == "bktrans")       { bkTrans_    = ParseBOOL(val);         return; }
    if (key == "showdirty")     { showDirty_  = ParseBOOL(val);         return; }
    if (key == "disabledfontcolor")   { disabledFontColor_  = ParseDWORD(val); return; }
    if (key == "defaultfontcolor")    { defaultFontColor_   = ParseDWORD(val); return; }
    if (key == "linkfontcolor")       { linkFontColor_      = ParseDWORD(val); return; }
    if (key == "linkhoverfontcolor")  { linkHoverFontColor_ = ParseDWORD(val); return; }
    if (key == "selectedcolor")       { selectedColor_      = ParseDWORD(val); return; }
    Container::SetAttribute(key, val);
}

void Window::DoLayout(const wxRect& rc) {
    auto& children = GetChildren();
    if (children.size() <= 1) {
        Container::DoLayout(rc);
        return;
    }

    wxRect inner{
        rc.x + inset_.x,
        rc.y + inset_.y,
        std::max(0, rc.width - inset_.x - inset_.width),
        std::max(0, rc.height - inset_.y - inset_.height)
    };

    int fixed = 0;
    int autoCount = 0;
    int visibleCount = 0;
    for (auto& child : children) {
        if (!child->IsVisible()) {
            continue;
        }
        ++visibleCount;
        const wxSize fixedSize = child->GetFixedSize();
        if (fixedSize.y > 0) {
            fixed += fixedSize.y;
        } else {
            ++autoCount;
        }
    }
    if (visibleCount > 1) {
        fixed += childPadding_ * (visibleCount - 1);
    }

    const int autoHeight =
        autoCount > 0 ? std::max(0, (inner.GetHeight() - fixed) / autoCount) : 0;
    int y = inner.y;
    bool first = true;
    for (auto& child : children) {
        if (!child->IsVisible()) {
            child->SetRect(wxRect(inner.x, y, inner.GetWidth(), 0));
            continue;
        }
        if (!first) {
            y += childPadding_;
        }
        first = false;
        const wxSize fixedSize = child->GetFixedSize();
        const int width = fixedSize.x > 0 ? fixedSize.x : inner.GetWidth();
        const int height = fixedSize.y > 0 ? fixedSize.y : autoHeight;
        child->SetRect(wxRect(inner.x, y, width, height));
        y += height;
    }
}

} // namespace wxui
