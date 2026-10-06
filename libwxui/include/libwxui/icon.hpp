#pragma once
#include "label.hpp"

namespace wxui {
    // Vector icons scale with the control; no platform emoji/font substitution.
    class Icon : public Button {
    public:
        void SetAttribute(const std::string& key, const std::string& value) override;
        void DoPaint(wxDC& dc, const wxRect& clipRect) override;
        std::string GetTag() const override {
            return "Icon";
        }

    private:
        std::string glyph_;
        int size_ = 16;
    };
}
