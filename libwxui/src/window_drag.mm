#include <wx/window.h>
#import <AppKit/AppKit.h>

namespace wxui {
bool BeginNativeWindowDrag(wxWindow* view) {
    NSView* native = (NSView*)view->GetHandle();
    NSWindow* window = [native window];
    NSEvent* event = [NSApp currentEvent];
    if (!window || [event type] != NSEventTypeLeftMouseDown || [event window] != window)
        return false;
    [window performWindowDragWithEvent:event];
    return true;
}
} // namespace wxui
