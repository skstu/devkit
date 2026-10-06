#include <libwxui.hpp>
#include "../graphics.hpp"
#include <wx/graphics.h>
#include <wx/dcmemory.h>
#include <wx/dcclient.h>

namespace wxui {
    void Icon::SetAttribute(const std::string& key, const std::string& value) {
        if (key == "glyph") {
            if (glyph_ != value) {
                glyph_ = value;
                Invalidate();
            }
            return;
        }
        if (key == "iconsize") {
            size_ = std::clamp(ParseINT(value, 16), 1, 256);
            return;
        }
        Button::SetAttribute(key, value);
    }
    void Icon::DoPaint(wxDC& dc, const wxRect& clipRect) {
        Button::DoPaint(dc, clipRect);
        GraphicsScope gc(dc);
        if (!gc)
            return;
        const auto color = !enabled_ && disabledTextColor_.IsOk() ? disabledTextColor_ : textColor_.IsOk() ? textColor_
                                                                                                           : *wxBLACK;
        const int size = std::min({size_, rect_.width, rect_.height});
        gc->Translate(rect_.x + (rect_.width - size) / 2.0, rect_.y + (rect_.height - size) / 2.0);
        gc->Scale(size / 24.0, size / 24.0);
        gc->SetPen(wxPen(color, 2));
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        auto line = [&](double x1, double y1, double x2, double y2) { gc->StrokeLine(x1, y1, x2, y2); };
        if (glyph_ == "circle") {
            gc->SetBrush(wxBrush(color));
            gc->DrawEllipse(3, 3, 18, 18);
        }
        else if (glyph_ == "unavailable") {
            gc->DrawEllipse(3, 3, 18, 18);
            line(5.5, 18.5, 18.5, 5.5);
        }
        else if (glyph_ == "info") {
            gc->DrawEllipse(2, 2, 20, 20);
            line(12, 11, 12, 18);
            line(12, 6, 12, 8);
        }
        else if (glyph_ == "add" || glyph_ == "close") {
            if (glyph_ == "add") {
                line(12, 4, 12, 20);
                line(4, 12, 20, 12);
            }
            else {
                line(5, 5, 19, 19);
                line(19, 5, 5, 19);
            }
        }
        else if (glyph_ == "chart" || glyph_ == "candlestick") {
            gc->SetBrush(wxBrush(color));
            gc->SetPen(*wxTRANSPARENT_PEN);
            if (glyph_ == "chart") {
                gc->DrawRectangle(3, 13, 4, 8);
                gc->DrawRectangle(10, 7, 4, 14);
                gc->DrawRectangle(17, 3, 4, 18);
            }
            else {
                gc->DrawRectangle(4, 6, 6, 11);
                gc->DrawRectangle(14, 9, 6, 10);
                gc->DrawRectangle(6, 2, 2, 20);
                gc->DrawRectangle(16, 4, 2, 19);
            }
        }
        else if (glyph_ == "launch") {
            auto p = gc->CreatePath();
            p.MoveToPoint(11, 3);
            p.AddLineToPoint(3, 3);
            p.AddLineToPoint(3, 21);
            p.AddLineToPoint(21, 21);
            p.AddLineToPoint(21, 13);
            gc->StrokePath(p);
            line(11, 13, 21, 3);
            line(14, 3, 21, 3);
            line(21, 3, 21, 10);
        }
        else if (glyph_ == "star" || glyph_ == "star-outline") {
            auto p = gc->CreatePath();
            p.MoveToPoint(12, 2);
            p.AddLineToPoint(15, 8);
            p.AddLineToPoint(22, 9);
            p.AddLineToPoint(17, 14);
            p.AddLineToPoint(18, 21);
            p.AddLineToPoint(12, 18);
            p.AddLineToPoint(6, 21);
            p.AddLineToPoint(7, 14);
            p.AddLineToPoint(2, 9);
            p.AddLineToPoint(9, 8);
            p.CloseSubpath();
            if (glyph_ == "star")
                gc->SetBrush(wxBrush(color));
            gc->DrawPath(p);
        }
        else if (glyph_ == "lock") {
            gc->DrawRoundedRectangle(5, 11, 14, 11, 2);
            gc->DrawRoundedRectangle(8, 3, 8, 11, 4);
        }
    }
}
