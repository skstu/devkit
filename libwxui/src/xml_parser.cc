#include <libwxui.hpp>

#include <wx/xml/xml.h>
#include <wx/sstream.h>
#include <wx/log.h>
#include <stdexcept>

namespace wxui {

// ── Forward declaration ───────────────────────────────────────────────────
static std::shared_ptr<Control> BuildControlFromNode(wxXmlNode* node,
                                                      UIManager* mgr);

// ── Recursive builder ─────────────────────────────────────────────────────
static std::shared_ptr<Control> BuildControlFromNode(wxXmlNode* node,
                                                      UIManager* mgr) {
    if (!node || node->GetType() != wxXML_ELEMENT_NODE)
        return {};

    const std::string rawTag = WxStringToUtf8(node->GetName());
    auto ctrl = ControlFactory::Instance().Create(rawTag);
    if (!ctrl) {
        wxLogWarning("libwxui: unknown tag <%s> — skipped", rawTag.c_str());
        return {};
    }

    // Apply XML attributes
    std::map<std::string, std::string> attributes;
    wxXmlAttribute* attr = node->GetAttributes();
    while (attr) {
        const auto name = NormalizeXmlIdentifier(WxStringToUtf8(attr->GetName()));
        const auto value = WxStringToUtf8(attr->GetValue());
        attributes[name] = value;
        if (name != "textid" && name != "hintid" && name != "tooltipid")
            ctrl->SetAttribute(name, value);
        attr = attr->GetNext();
    }
    // 第二遍绑定语言键；text/textid 的 XML 属性顺序不会影响原文回退。
    for (const std::string name : {"text", "hint", "tooltip"}) {
        const auto key = attributes.find(name + "id");
        if (key != attributes.end()) ctrl->BindTranslation(name, key->second, attributes[name]);
    }

    // Propagate manager pointer after SetAttribute so native-backed controls
    // are created with the final XML style/text values already available.
    ctrl->SetManager(mgr);

    // Recurse into children (only meaningful for Container subclasses)
    auto* container = dynamic_cast<Container*>(ctrl.get());
    if (container) {
        for (wxXmlNode* child = node->GetChildren();
             child;
             child = child->GetNext()) {
            if (child->GetType() != wxXML_ELEMENT_NODE) continue;
            auto childCtrl = BuildControlFromNode(child, mgr);
            if (childCtrl) container->Add(childCtrl);
        }
    }

    return ctrl;
}

std::shared_ptr<Control> CreateControlFromXml(const std::string& xml) {
    wxStringInputStream stream(Utf8ToWxString(xml));
    wxXmlDocument doc;
    if (!doc.Load(stream))
        throw std::invalid_argument("Invalid control XML");
    if (!ControlFactory::Instance().HasTag("Window"))
        ControlFactory::RegisterBuiltins();
    const auto validate = [&](const auto& self, wxXmlNode* node) -> void {
        if (node->GetType() != wxXML_ELEMENT_NODE) return;
        const auto tag = WxStringToUtf8(node->GetName());
        if (!ControlFactory::Instance().HasTag(tag) &&
            !ControlFactory::Instance().HasTag(NormalizeXmlIdentifier(tag)))
            throw std::invalid_argument("Unknown control XML tag: " + tag);
        for (auto* child = node->GetChildren(); child; child = child->GetNext())
            self(self, child);
    };
    validate(validate, doc.GetRoot());
    auto control = BuildControlFromNode(doc.GetRoot(), nullptr);
    if (!control)
        throw std::invalid_argument("Unknown control XML root");
    return control;
}

// ── UIManager — LoadFromFile ──────────────────────────────────────────────
bool UIManager::LoadFromFile(const std::string& xmlPath) {
    wxXmlDocument doc;
    if (!doc.Load(Utf8ToWxString(xmlPath))) {
        wxLogError("libwxui: failed to load XML from '%s'", xmlPath.c_str());
        return false;
    }
    return BuildFromXmlDoc(doc);
}

bool UIManager::LoadFromResource(const std::string& xmlPath) {
    std::string xml;
    if (LoadResourceBytes(xmlPath, &xml)) {
        return LoadFromString(xml);
    }

    const std::string full = resRoot_.empty() ? xmlPath : resRoot_ + "/" + xmlPath;
    return LoadFromFile(full);
}

// ── UIManager — LoadFromString ────────────────────────────────────────────
bool UIManager::LoadFromString(const std::string& xmlContent) {
    wxStringInputStream stream(Utf8ToWxString(xmlContent));
    wxXmlDocument doc;
    if (!doc.Load(stream)) {
        wxLogError("libwxui: failed to parse inline XML");
        return false;
    }
    return BuildFromXmlDoc(doc);
}

// ── UIManager — BuildFromXmlDoc ───────────────────────────────────────────
bool UIManager::BuildFromXmlDoc(wxXmlDocument& doc) {
    wxXmlNode* root = doc.GetRoot();
    if (!root) return false;

    auto ctrl = BuildControlFromNode(root, this);
    if (!ctrl) return false;

    auto* win = dynamic_cast<Window*>(ctrl.get());
    ForgetControlTree(root_.get());
    if (win) {
        root_ = std::static_pointer_cast<Window>(ctrl);
    } else {
        wxLogWarning("libwxui: root element <%s> is not <Window> — wrapping",
                     root->GetName().c_str());
        root_ = std::make_shared<Window>();
        root_->Add(ctrl);
    }

    root_->SetManager(this);
    const wxColour rootBk = root_->GetBkColor();
    if (rootBk.IsOk() && rootBk.Alpha() != 0) {
        SetBackgroundColour(rootBk);
    }
    DoLayout();
    return true;
}

} // namespace wxui
