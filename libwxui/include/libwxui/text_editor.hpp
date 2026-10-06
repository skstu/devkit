#pragma once
// Internal native adapter shared by RichEdit and JsonViewer. Applications use
// those logical controls; all native editing and scrollbar details stay here.
#include <wx/panel.h>
#include <wx/stc/stc.h>
#include <wx/textctrl.h>
#include <map>
#include <functional>

namespace wxui {
class EditorScrollBar;
class TextEditor : public wxPanel {
public:
    TextEditor(wxWindow* parent, wxWindowID id, const wxString& value,
               const wxPoint& position, const wxSize& size, long style);
    wxString GetValue() const;
    void SetValue(const wxString& text);
    void AppendText(const wxString& text);
    void Clear() { SetValue({}); }
    void SetEditable(bool editable);
    bool IsEditable() const;
    long GetLastPosition() const;
    void SetInsertionPoint(long position);
    void SetInsertionPointEnd();
    void ShowPosition(long position);
    void SetSelection(long from, long to);
    void Copy();
    void Paste();
    bool SetFont(const wxFont& font) override;
    void SetOwnBackgroundColour(const wxColour& color);
    void SetOwnForegroundColour(const wxColour& color);
    void SetDefaultStyle(const wxTextAttr& style);
    wxTextAttr GetDefaultStyle() const { return defaultStyle_; }
    void SetStyle(long from, long to, const wxTextAttr& style);
    void SetJsonHighlight(bool enabled);
    void SetWrap(bool wrap);
    void SetFocus() override;
    void UpdateScrollbars();
    wxStyledTextCtrl* Editor() const { return editor_; }
private:
    int BytePosition(long position) const;
    void ApplyAppearance();
    wxStyledTextCtrl* editor_ = nullptr;
    wxTextCtrl* plain_ = nullptr;
    EditorScrollBar *vertical_ = nullptr, *horizontal_ = nullptr;
    wxTextAttr defaultStyle_;
    wxColour background_{255,255,255}, foreground_{23,43,58};
    std::map<unsigned long, int> colorStyles_;
    bool json_ = false, wrap_ = true, updating_ = false, setting_ = false;
};
} // namespace wxui
