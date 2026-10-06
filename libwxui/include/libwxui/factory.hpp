#pragma once
/// libwxui — ControlFactory: maps XML tag names → Control instances.

#include "types.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace wxui {

class Control;

using ControlCreateFn = std::function<std::shared_ptr<Control>()>;

// Build a detached XML subtree. Attach it with Container::Add on the UI thread.
// No native children are created until attachment. Invalid XML/tags throw.
std::shared_ptr<Control> CreateControlFromXml(const std::string& xml);

class ControlFactory {
public:
    /// Process-lifetime singleton.
    static ControlFactory& Instance();

    /// Register a tag → factory function mapping.
    void Register(const std::string& tag, ControlCreateFn fn);

    /// Create a control for the given XML tag; returns nullptr if unknown.
    [[nodiscard]] std::shared_ptr<Control> Create(const std::string& tag) const;

    [[nodiscard]] bool HasTag(const std::string& tag) const {
        return registry_.count(tag) > 0;
    }

    /// Register all 29 built-in controls. Call once before LoadFromFile.
    static void RegisterBuiltins();

private:
    std::unordered_map<std::string, ControlCreateFn> registry_;
};

} // namespace wxui
