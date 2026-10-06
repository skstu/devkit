#pragma once
/// libwxui — lightweight event / notification system.
/// Controls fire "notifies" (string names) which map to user callbacks.

#include <functional>
#include <string>
#include <unordered_map>

namespace wxui {

class Control;

/// Data passed to every notify callback.
struct NotifyEvent {
    Control*    sender   = nullptr; ///< The control that fired the event
    std::string notify;             ///< Notify name: "click", "valuechanged", …
    int         x        = 0;      ///< Mouse X relative to UIManager (if applicable)
    int         y        = 0;      ///< Mouse Y relative to UIManager (if applicable)
    int         param1   = 0;      ///< Integer payload (new value, item index, …)
    std::string strParam;           ///< String payload (link href, text, …)
};

using NotifyCallback = std::function<void(const NotifyEvent&)>;

/// Per-control event sink: maps notify names → callbacks.
class EventSink {
public:
    void Bind(const std::string& notify, NotifyCallback cb) {
        handlers_[notify] = std::move(cb);
    }
    void Unbind(const std::string& notify) { handlers_.erase(notify); }

    void Fire(const NotifyEvent& ev) const {
        if (auto it = handlers_.find(ev.notify); it != handlers_.end())
        {
            // A handler may rebuild and remove its own control tree.
            const auto callback = it->second;
            callback(ev);
        }
    }

    [[nodiscard]] bool Has(const std::string& notify) const {
        return handlers_.count(notify) > 0;
    }

private:
    std::unordered_map<std::string, NotifyCallback> handlers_;
};

} // namespace wxui
