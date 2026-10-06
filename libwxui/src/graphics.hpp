#pragma once
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/dcclient.h>
#include <wx/graphics.h>
#include <memory>

namespace wxui {
    // One graphics canvas per paint pass. Standalone control rendering can still
    // acquire its own canvas; normal painting borrows the wxGCDC canvas.
    class GraphicsScope {
        std::unique_ptr<wxGraphicsContext> owned_;
        wxGraphicsContext* context_ = nullptr;

    public:
        explicit GraphicsScope(wxDC& dc) {
            context_ = dc.GetGraphicsContext();
            if (context_) {
                context_->PushState();
                return;
            }
            if (auto* memory = dynamic_cast<wxMemoryDC*>(&dc))
                owned_.reset(wxGraphicsContext::Create(*memory));
            else if (auto* window = dynamic_cast<wxWindowDC*>(&dc))
                owned_.reset(wxGraphicsContext::Create(*window));
            if (!context_)
                context_ = owned_.get();
            if (context_)
                context_->PushState();
        }
        ~GraphicsScope() {
            if (context_)
                context_->PopState();
        }
        explicit operator bool() const {
            return context_ != nullptr;
        }
        wxGraphicsContext* operator->() const {
            return context_;
        }
    };
}
