#pragma once
#include "label.hpp"
#include <optional>

namespace wxui {
    class PricePlot : public Label {
    public:
        struct Bar {
            std::string date, open, high, low, close;
        };
        void SetSnapshot(std::vector<std::pair<std::string, std::string>> values);
        void SetBars(std::vector<Bar> bars);
        void SetCandles(bool candles) {
            candles_ = candles;
            Invalidate();
        }
        void DoPaint(wxDC& dc, const wxRect& clipRect) override;
        void OnMouseMove(const wxPoint& point) override;
        void OnMouseLeave() override;
        std::string GetTag() const override {
            return "PricePlot";
        }

    private:
        std::vector<std::pair<std::string, std::string>> values_;
        std::vector<Bar> bars_;
        std::optional<wxPoint> pointer_;
        bool candles_ = true;
    };
}
