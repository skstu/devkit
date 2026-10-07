#include <libwxui.hpp>

namespace wxui {

// ── Singleton ─────────────────────────────────────────────────────────────
ControlFactory& ControlFactory::Instance() {
    static ControlFactory instance;
    return instance;
}

void ControlFactory::Register(const std::string& tag, ControlCreateFn fn) {
    const std::string normalized = NormalizeXmlIdentifier(tag);
    registry_[tag] = fn;
    if (!normalized.empty() && normalized != tag) {
        registry_[normalized] = fn;
    }
}

std::shared_ptr<Control> ControlFactory::Create(const std::string& tag) const {
    auto it = registry_.find(tag);
    if (it == registry_.end()) {
        const std::string normalized = NormalizeXmlIdentifier(tag);
        it = registry_.find(normalized);
    }
    if (it == registry_.end()) return {};
    return it->second();
}

// ── RegisterBuiltins ──────────────────────────────────────────────────────
#define REG(Tag, Class) \
    Instance().Register(Tag, []() -> std::shared_ptr<Control> { \
        return std::make_shared<Class>(); \
    })

void ControlFactory::RegisterBuiltins() {
    // Base / layout
    REG("Control",               Control);
    REG("Container",             Container);
    REG("VerticalLayout",        VerticalLayout);
    REG("HorizontalLayout",      HorizontalLayout);
    REG("TileLayout",            TileLayout);
    REG("TabLayout",             TabLayout);
    REG("ChildLayout",           ChildLayout);
    REG("Window",                Window);

    // Label family
    REG("Label",                 Label);
    REG("DecimalLabel",          DecimalLabel);
    REG("Icon",                  Icon);
    REG("PricePlot",             PricePlot);
    REG("Text",                  Text);
    REG("Button",                Button);
    REG("Option",                Option);
    REG("Progress",              Progress);
    REG("Slider",                Slider);
    REG("Edit",                  Edit);

    REG("FormEdit",              FormEdit);
    REG("NativeChoice",          NativeChoice);
    REG("PaneCanvas",            PaneCanvas);
    REG("ResizablePane",         ResizablePane);
    REG("DecimalButton",         DecimalButton);
    REG("StackIconButton",        StackIconButton);

    // ScrollBar
    REG("ScrollBar",             ScrollBar);

    // Combo
    REG("Combo",                 Combo);

    // List family
    REG("List",                  List);
    REG("ListHeader",            ListHeader);
    REG("ListHeaderItem",        ListHeaderItem);
    REG("ListLabelElement",      ListLabelElement);
    REG("ListTextElement",       ListTextElement);
    REG("ListContainerElement",  ListContainerElement);

    // RichEdit
    REG("RichEdit",              RichEdit);

    // JSON viewer
    REG("JsonViewer",            JsonViewer);

    // TreeView
    REG("TreeView",              TreeView);
    REG("TreeNode",              TreeNode);

    // GifAnim
    REG("GifAnim",               GifAnim);

    // Html
    REG("HtmlWindow",            HtmlWindow);
    REG("Html",                  HtmlWindow);

    // ActiveX / WebBrowser
    REG("ActiveX",               ActiveX);
    REG("WebBrowser",            WebBrowser);
}

#undef REG

} // namespace wxui
