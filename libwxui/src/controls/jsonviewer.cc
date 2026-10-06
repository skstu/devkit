#include <libwxui.hpp>
#include <libwxui/text_editor.hpp>
#include <libwxui/appearance.hpp>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace wxui {
namespace {

std::string JsonToText(const rapidjson::Value& value, bool pretty) {
    rapidjson::StringBuffer buffer;
    if (pretty) {
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        value.Accept(writer);
    } else {
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        value.Accept(writer);
    }
    return std::string(buffer.GetString(), buffer.GetSize());
}

std::string NormalizeJson(std::string_view raw, JsonViewMode mode) {
    if (mode == JsonViewMode::Raw || raw.empty()) {
        return std::string(raw);
    }

    rapidjson::Document doc;
    if (doc.Parse(raw.data(), raw.size()).HasParseError()) {
        return std::string(raw);
    }
    return JsonToText(doc, mode == JsonViewMode::Pretty);
}

JsonViewMode ParseJsonViewMode(const std::string& value, JsonViewMode fallback) {
    const std::string normalized = NormalizeXmlIdentifier(value);
    if (normalized == "raw") return JsonViewMode::Raw;
    if (normalized == "min" || normalized == "mini" ||
        normalized == "minify" || normalized == "minified" ||
        normalized == "compact") {
        return JsonViewMode::Minified;
    }
    if (normalized == "format" || normalized == "formatted" ||
        normalized == "pretty") {
        return JsonViewMode::Pretty;
    }
    return fallback;
}

wxColour ResolveJsonTextColor(UIManager* manager, const wxColour& requested) {
    if (requested.IsOk()) return requested;
    if (manager && manager->GetRoot()) {
        return manager->GetRoot()->GetDefaultFontColor();
    }
    return *wxWHITE;
}

wxFont ResolveJsonFont(UIManager*) { return CodeFont(); }

} // namespace

JsonViewer::~JsonViewer() = default;

void JsonViewer::SetAttribute(const std::string& key, const std::string& val) {
    if (key == "text" || key == "json" || key == "rawjson" || key == "rawJson") {
        SetJson(val);
        return;
    }
    if (key == "mode" || key == "viewmode" || key == "viewMode") {
        SetMode(ParseJsonViewMode(val, mode_));
        return;
    }
    if (key == "wraplines" || key == "wrapLines" || key == "wrap") {
        SetWrapLines(ParseBOOL(val, true));
        return;
    }
    if (key == "transparent") {
        transparent_ = ParseBOOL(val, true);
        return;
    }
    if (key == "textcolor") {
        textColor_ = ParseDWORD(val);
        SyncStyle();
        return;
    }
    if (key == "font") {
        fontId_ = ParseINT(val, -1);
        SyncStyle();
        return;
    }
    Container::SetAttribute(key, val);
    if (key == "tooltip") SyncStyle();
}

void JsonViewer::OnManagerSet() {
    CreateNativeCtrls();
}

void JsonViewer::CreateNativeCtrls() {
    if (!manager_ || wrappedCtrl_) return;

    const long baseStyle = wxBORDER_NONE | wxTE_MULTILINE | wxTE_READONLY |
                           wxTE_RICH2 | wxTE_PROCESS_TAB | wxVSCROLL;
    wrappedCtrl_ = new TextEditor(manager_, wxID_ANY, wxString{},
                                  rect_.GetTopLeft(), rect_.GetSize(),
                                  baseStyle);
    nowrapCtrl_ = new TextEditor(manager_, wxID_ANY, wxString{},
                                 rect_.GetTopLeft(), rect_.GetSize(),
                                 baseStyle | wxTE_DONTWRAP | wxHSCROLL);
    SyncStyle();
    RefreshText();
}

void JsonViewer::SyncStyle() {
    for (TextEditor* ctrl : {wrappedCtrl_, nowrapCtrl_}) {
        if (!ctrl) continue;
        ctrl->SetToolTip(Utf8ToWxString(tooltip_));
        ctrl->SetFont(ResolveJsonFont(manager_));
        ctrl->SetOwnForegroundColour(ResolveJsonTextColor(manager_, textColor_));
        if (bkColor_.IsOk() && bkColor_.Alpha() != 0) {
            ctrl->SetOwnBackgroundColour(bkColor_);
        }
        ctrl->SetEditable(false);
        ctrl->Enable(enabled_);
        ctrl->Show(IsNativeWindowVisible() &&
                   ((ctrl == wrappedCtrl_) == wrapLines_));
    }
}

void JsonViewer::SetRect(const wxRect& rc) {
    Control::SetRect(rc);
    const int inset = std::max(0, borderSize_);
    const wxRect inner(rc.x + inset, rc.y + inset,
                       std::max(0, rc.width - inset * 2),
                       std::max(0, rc.height - inset * 2));
    for (TextEditor* ctrl : {wrappedCtrl_, nowrapCtrl_}) {
        if (!ctrl) continue;
        if (ctrl->GetRect() != inner) ctrl->SetSize(inner);
        ctrl->Show(IsNativeWindowVisible() &&
                   ((ctrl == wrappedCtrl_) == wrapLines_));
    }
}

void JsonViewer::SetVisible(bool v) {
    if (IsVisible() == v) return;
    Control::SetVisible(v);
    SyncStyle();
}

void JsonViewer::SetEnabled(bool e) {
    Control::SetEnabled(e);
    SyncStyle();
}

void JsonViewer::SetJson(std::string_view json) {
    rawJson_ = std::string(json);
    prettyJson_.clear();
    minifiedJson_.clear();
    prettyJsonValid_ = false;
    minifiedJsonValid_ = false;
    RefreshText();
}

void JsonViewer::SetMode(JsonViewMode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    RefreshText();
}

void JsonViewer::SetWrapLines(bool wrap) {
    if (wrapLines_ == wrap) return;
    wrapLines_ = wrap;
    SyncStyle();
    RenderActiveText();
    if (auto* ctrl = ActiveTextCtrl()) {
        ctrl->SetFocus();
    }
}

std::string JsonViewer::GetDisplayedText() const {
    return displayedText_;
}

TextEditor* JsonViewer::ActiveTextCtrl() const {
    return wrapLines_ ? wrappedCtrl_ : nowrapCtrl_;
}

bool& JsonViewer::ActiveRenderedFlag() {
    return wrapLines_ ? wrappedRendered_ : nowrapRendered_;
}

void JsonViewer::InvalidateRenderedControls() {
    wrappedRendered_ = false;
    nowrapRendered_ = false;
}

const std::string& JsonViewer::TextForMode(JsonViewMode mode) {
    if (mode == JsonViewMode::Raw) return rawJson_;
    if (mode == JsonViewMode::Pretty) {
        if (!prettyJsonValid_) {
            prettyJson_ = NormalizeJson(rawJson_, JsonViewMode::Pretty);
            prettyJsonValid_ = true;
        }
        return prettyJson_;
    }
    if (!minifiedJsonValid_) {
        minifiedJson_ = NormalizeJson(rawJson_, JsonViewMode::Minified);
        minifiedJsonValid_ = true;
    }
    return minifiedJson_;
}

void JsonViewer::RefreshText() {
    displayedText_ = TextForMode(mode_);
    InvalidateRenderedControls();
    RenderActiveText();
}

void JsonViewer::RenderActiveText() {
    TextEditor* ctrl = ActiveTextCtrl();
    if (!ctrl) return;
    bool& rendered = ActiveRenderedFlag();
    if (rendered) return;
    SetTextCtrlValue(ctrl, Utf8ToWxString(displayedText_));
    rendered = true;
}

void JsonViewer::SetTextCtrlValue(TextEditor* ctrl, const wxString& text) {
    if (!ctrl) return;
    ctrl->Freeze();
    ctrl->SetEditable(true);
    ctrl->SetValue(text);
    ctrl->SetInsertionPoint(0);
    ctrl->ShowPosition(0);
    ctrl->SetEditable(false);
    ApplyJsonHighlight(ctrl, text);
    ctrl->Thaw();
}

void JsonViewer::ApplyJsonHighlight(TextEditor* ctrl, const wxString&) {
    if (ctrl) ctrl->SetJsonHighlight(true);
}

void JsonViewer::DoPaint(wxDC& dc, const wxRect& clipRect) {
    if (!transparent_) Control::DoPaint(dc, clipRect);
    DrawBorder(dc);
}

} // namespace wxui
