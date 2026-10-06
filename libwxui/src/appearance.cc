#include <libwxui/appearance.hpp>
#include <wx/fontenum.h>
#include <wx/settings.h>
#include <mutex>
#include <algorithm>

namespace wxui {
namespace {
wxFont uiFont, codeFont;
std::once_flag initialized;
}
void InitializeTypography() {
    std::call_once(initialized, [] {
        // Let the OS choose its native UI face and CJK fallback fonts.
        uiFont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
#ifdef __WXMSW__
        if (wxFontEnumerator::IsValidFacename("Microsoft YaHei"))
            uiFont.SetFaceName("Microsoft YaHei");
        uiFont.SetPointSize(11);
        codeFont = uiFont;
#else
        codeFont = wxFont(wxFontInfo(uiFont.GetPointSize()).Family(wxFONTFAMILY_TELETYPE));
#endif
    });
}
wxFont InterfaceFont() { InitializeTypography(); return uiFont; }
wxFont FontWithLogicalSize(wxFont font, double pixels) {
    // Cocoa points are logical pixels; the other ports use the 96-DPI basis.
    // wxWidgets applies the native display scale to point-sized fonts.
#if defined(__WXOSX__)
    font.SetFractionalPointSize(pixels);
#else
    font.SetFractionalPointSize(pixels * 72.0 / 96.0);
#endif
    return font;
}
wxFont CodeFont() { InitializeTypography(); return codeFont; }
ChromeColors ChromeFor(const wxColour& background) {
    const bool dark = background.IsOk() &&
        (background.Red() * 299 + background.Green() * 587 + background.Blue() * 114 < 128000);
    if (dark) return {{38, 45, 55}, {99, 113, 130}, {137, 165, 191}, {73, 87, 103}};
    return {{240, 244, 248}, {174, 187, 201}, {99, 134, 163}, {200, 211, 222}};
}
void PaintScrollChrome(wxDC& dc, const wxRect& track, const wxRect& thumb,
                       bool active, const wxColour& background) {
    const auto colors = ChromeFor(background);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(colors.track));
    dc.DrawRectangle(track);
    if (thumb.IsEmpty()) return;
    dc.SetBrush(wxBrush(active ? colors.active : colors.thumb));
    dc.DrawRoundedRectangle(thumb, std::min(thumb.width, thumb.height) / 2.0);
}
void PaintDivider(wxDC& dc, const wxRect& rect, bool vertical, bool active,
                  const wxColour& background) {
    const auto colors = ChromeFor(background);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(background.IsOk() ? background : colors.track));
    dc.DrawRectangle(rect);
    dc.SetPen(wxPen(active ? colors.active : colors.divider));
    const wxPoint center(rect.x + rect.width / 2, rect.y + rect.height / 2);
    if (vertical) dc.DrawLine(center.x, rect.y, center.x, rect.GetBottom() + 1);
    else dc.DrawLine(rect.x, center.y, rect.GetRight() + 1, center.y);
    dc.SetBrush(wxBrush(active ? colors.active : colors.thumb));
    dc.SetPen(*wxTRANSPARENT_PEN);
    if (vertical) dc.DrawRoundedRectangle(center.x - 1, center.y - 12, 3, 24, 1);
    else dc.DrawRoundedRectangle(center.x - 12, center.y - 1, 24, 3, 1);
}
} // namespace wxui
