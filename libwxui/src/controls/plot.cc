#include <libwxui.hpp>
#include <cmath>
#include <limits>

namespace wxui {
    namespace {
        wxRect plotArea(const wxRect& rect, bool bars) {
            return {rect.x + 16, rect.y + (bars ? 12 : 60), std::max(1, rect.width - 112), std::max(1, rect.height - (bars ? 40 : 100))};
        }
        double value(const std::string& text) {
            double number = 0;
            return Utf8ToWxString(text).ToDouble(&number) && std::isfinite(number) ? number : std::numeric_limits<double>::quiet_NaN();
        }
    }
    void PricePlot::SetSnapshot(std::vector<std::pair<std::string, std::string>> values) {
        values_ = std::move(values);
        bars_.clear();
        Invalidate();
    }
    void PricePlot::SetBars(std::vector<Bar> bars) {
        bars_ = std::move(bars);
        values_.clear();
        Invalidate();
    }
    void PricePlot::OnMouseMove(const wxPoint& point) {
        pointer_ = point;
        const auto area = plotArea(rect_, !bars_.empty());
        if (!area.Contains(point))
            pointer_.reset();
        if (!bars_.empty() && area.Contains(point)) {
            const int index = std::clamp(int(double(point.x - area.x) / area.width * bars_.size()), 0, int(bars_.size()) - 1);
            FireNotify("pointchange", point.x, point.y, index);
        }
        Invalidate();
    }
    void PricePlot::OnMouseLeave() {
        pointer_.reset();
        Invalidate();
    }
    void PricePlot::DoPaint(wxDC& dc, const wxRect&) {
        DrawBkColor(dc);
        const auto area = plotArea(rect_, !bars_.empty());
        dc.SetPen(wxPen(wxColour(226, 232, 238)));
        for (int i = 0; i <= 6; ++i) {
            const int x = area.x + area.width * i / 6, y = area.y + area.height * i / 6;
            dc.DrawLine(x, area.y, x, area.GetBottom());
            dc.DrawLine(area.x, y, area.GetRight(), y);
        }
        double low = std::numeric_limits<double>::infinity(), high = -low;
        for (const auto& [label, text] : values_) {
            const double v = value(text);
            if (std::isfinite(v)) {
                low = std::min(low, v);
                high = std::max(high, v);
            }
        }
        for (const auto& b : bars_) {
            if (std::isfinite(value(b.low)) && std::isfinite(value(b.high))) {
                low = std::min(low, value(b.low));
                high = std::max(high, value(b.high));
            }
        }
        if (std::isfinite(low) && std::isfinite(high)) {
            const double margin = high > low ? (high - low) * .4 : std::max(std::abs(high) * .002, .001);
            const auto y = [&](double p) { return area.GetBottom() - int((p - low + margin) / (high - low + 2 * margin) * area.height); };
            dc.SetFont(ResolveFont(10));
            dc.SetTextForeground(wxColour(105, 121, 133));
            if (!bars_.empty()) {
                wxPoint previous;
                bool havePrevious = false;
                const double dx = double(area.width) / bars_.size();
                for (size_t i = 0; i < bars_.size(); ++i) {
                    const auto& b = bars_[i];
                    const double open = value(b.open), close = value(b.close), hi = value(b.high), lo = value(b.low);
                    if (!std::isfinite(open) || !std::isfinite(close) || !std::isfinite(hi) || !std::isfinite(lo)) {
                        havePrevious = false;
                        continue;
                    }
                    const int x = area.x + int(dx * (i + .5));
                    const wxColour color = close >= open ? wxColour(190, 71, 61) : wxColour(46, 128, 93);
                    dc.SetPen(wxPen(candles_ ? color : wxColour(53, 108, 145)));
                    if (candles_) {
                        dc.DrawLine(x, y(hi), x, y(lo));
                        dc.SetBrush(wxBrush(color));
                        const int w = std::clamp(int(dx * .6), 1, 12);
                        dc.DrawRectangle(x - w / 2, std::min(y(open), y(close)), w, std::max(1, std::abs(y(open) - y(close))));
                    }
                    else if (havePrevious)
                        dc.DrawLine(previous, wxPoint(x, y(close)));
                    previous = {x, y(close)};
                    havePrevious = true;
                }
            }
            else {
                auto sorted = values_;
                std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return value(a.second) > value(b.second); });
                int previousY = area.y - 20;
                const int x = area.x + int(area.width * .58);
                for (const auto& [name, text] : sorted) {
                    const auto v = value(text);
                    if (!std::isfinite(v))
                        continue;
                    const int py = y(v);
                    const wxColour color = name == "ASK" ? wxColour(166, 104, 37) : wxColour(53, 108, 145);
                    dc.SetPen(wxPen(color));
                    dc.SetBrush(wxBrush(color));
                    dc.DrawCircle(x, py, 4);
                    dc.DrawLine(x + 7, py, area.GetRight(), py);
                    previousY = std::max(py - 6, previousY + 16);
                    dc.DrawText(Utf8ToWxString(name + " " + text), area.GetRight() + 6, previousY);
                }
            }
        }
        else {
            dc.SetFont(ResolveFont(16));
            dc.SetTextForeground(wxColour(105, 121, 133));
            dc.DrawLabel("暂无报价", area, wxALIGN_CENTER);
        }
        if (pointer_ && area.Contains(*pointer_)) {
            dc.SetPen(wxPen(wxColour(150, 163, 174), 1, wxPENSTYLE_DOT));
            dc.DrawLine(pointer_->x, area.y, pointer_->x, area.GetBottom());
            dc.DrawLine(area.x, pointer_->y, area.GetRight(), pointer_->y);
        }
    }
}
