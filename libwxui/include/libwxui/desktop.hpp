#pragma once

#include "control.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>

namespace wxui {

struct Extent { int width = 0; int height = 0; };
struct DesktopWindowSpec {
    std::string title;
    Extent size{1000, 700};
    Extent minimumSize{640, 480};
    int statusFields = 1;
    bool standardMenu = false;
};

// A copyable, thread-safe sender. Queued callbacks are discarded on window
// close; retained senders safely return false after their owner is destroyed.
using UiPost = std::function<bool(std::function<void()>)>;
using DesktopResourceLoader = std::function<bool(const std::string&, std::string*)>;

// Composition keeps native window methods out of application controllers.
// All methods except the sender returned by Poster() run on the UI thread.
class DesktopWindow {
public:
    DesktopWindow(const DesktopWindowSpec& spec, const std::string& xml,
                  DesktopResourceLoader resources = {});
    ~DesktopWindow();
    DesktopWindow(const DesktopWindow&) = delete;
    DesktopWindow& operator=(const DesktopWindow&) = delete;

    Control* FindControl(const std::string& name) const;
    template<class T> T* Require(const std::string& name) const {
        auto* control = dynamic_cast<T*>(FindControl(name));
        if (!control) throw std::runtime_error("Missing or incompatible UI control: " + name);
        return control;
    }
    void Present();
    void SetStatus(const std::string& text, int field = 0);
    void SetTitle(const std::string& text);
    void LoadLanguageXml(const std::string& language, const std::string& xml);
    void LoadLanguageResource(const std::string& language, const std::string& resource);
    void SetLanguage(const std::string& language);
    void SetFallbackLanguage(const std::string& language);
    const std::string& GetLanguage() const;
    std::string Translate(const std::string& key, const std::string& fallback = {}) const;
    Extent ClientExtent() const;
    void RefreshLayout();
    UiPost Poster() const;
    // Return false to defer a vetoable close. Forced closes must stop producers
    // synchronously; the callback is always informed whether deferral is possible.
    void OnClose(std::function<bool(bool canDefer)> callback);
    void RequestClose();
    void FinishClose();

    std::optional<std::string> OpenFile(const std::string& title,
                                      const std::string& filter = "All files|*");
    std::optional<std::string> SaveFile(const std::string& title,
                                      const std::string& filename,
                                      const std::string& filter);
    std::optional<std::string> ChooseDirectory(const std::string& title);
    bool Confirm(const std::string& title, const std::string& message);
    void Error(const std::string& title, const std::string& message);
    void ShowText(const std::string& title, std::string_view text);
    // Modal XML content with a logical control tree; caller never needs wx handles.
    bool ShowDialog(const std::string& title, const std::string& xml, Extent size,
                    std::function<void(Control&)> initialize = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::string ExecutablePath();
std::string HostName();
void ShowError(const std::string& title, const std::string& message);

} // namespace wxui
