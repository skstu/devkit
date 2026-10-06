#include <libwxui.hpp>
#include <libwxui/text_editor.hpp>
#include <libwxui/appearance.hpp>
#include "assets.hpp"
#include <wx/fontenum.h>
#include <wx/settings.h>
#include <iostream>
#include <thread>
#include <wx/dcmemory.h>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class WindowActivity : public wxEvtHandler {
public:
    explicit WindowActivity(wxWindow* window) : window_(window) {
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { ++sizes; event.Skip(); });
        Bind(wxEVT_PAINT, [this](wxPaintEvent& event) { ++paints; event.Skip(); });
        Bind(wxEVT_ERASE_BACKGROUND, [this](wxEraseEvent& event) { ++erases; event.Skip(); });
        window_->PushEventHandler(this);
    }
    ~WindowActivity() override { window_->RemoveEventHandler(this); }
    int sizes = 0, paints = 0, erases = 0;
private:
    wxWindow* window_;
};

class DesktopTest : public wxui::Application {
    std::unique_ptr<wxui::DesktopWindow> window_;
    wxui::UiPost sender_;
    int failures_ = 0;
    int delivered_ = 0;

    bool OnAppInit() override {
        window_ = std::make_unique<wxui::DesktopWindow>(
            wxui::DesktopWindowSpec{"libwxui tests", {1260, 860}, {980, 700}, 2}, devtools::assets::Get("workbench.xml"), devtools::assets::Load);
        window_->Present();
        sender_ = window_->Poster();
        std::thread producer([this] { sender_([this] { run(); }); });
        producer.join();
        return true;
    }
    void run() {
        try {
            ++delivered_;
            window_->RefreshLayout();
            check(wxui::InterfaceFont().IsOk() && wxui::CodeFont().IsOk(), "system fonts available");
#ifdef __WXMSW__
            if (wxFontEnumerator::IsValidFacename("Microsoft YaHei"))
                check(wxui::InterfaceFont().GetFaceName() == "Microsoft YaHei", "Windows uses Microsoft YaHei");
#else
            check(wxui::InterfaceFont().GetFaceName() == wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).GetFaceName(), "native system UI font");
#endif
            std::string embedded;
            check(window_->Require<wxui::Control>("title")->GetManager()->LoadResourceBytes("banner.xml", &embedded), "resource loader wired into UI");
            check(embedded == devtools::assets::Get("banner.xml"), "embedded XML bytes match");
            check(!devtools::assets::Notices().empty(), "licenses included in executable");
            // Verify actual painted glyph bounds, not just alignment attributes.
            wxBitmap sample(160, 60);
            wxMemoryDC dc(sample); dc.SetBackground(*wxWHITE_BRUSH); dc.Clear();
            wxui::Label label;
            label.SetManager(window_->Require<wxui::Control>("title")->GetManager());
            label.SetText("Ag中文"); label.SetAttribute("align", "center");
            label.SetRect({0, 0, 160, 60}); label.DoPaint(dc, {0, 0, 160, 60});
            dc.SelectObject(wxNullBitmap);
            const auto pixels = sample.ConvertToImage();
            int top = 60, bottom = -1;
            for (int y = 0; y < 60; ++y) for (int x = 0; x < 160; ++x) {
                if (pixels.GetRed(x,y) < 160) { top = std::min(top,y); bottom = std::max(bottom,y); }
            }
            check(bottom >= top && std::abs((top + bottom) / 2 - 30) <= 6, "painted text vertically centered");
            auto* request = window_->Require<wxui::RichEdit>("request");
            auto* response = window_->Require<wxui::JsonViewer>("response");
            check(request->GetWidth() >= 200 && response->GetWidth() >= 200, "editor layout");
            check(std::abs(request->GetWidth() - response->GetWidth()) <= 1, "equal initial split");
            auto* splitter = window_->Require<wxui::HorizontalLayout>("editors");
            const int originalWidth = request->GetWidth();
            const auto requestRect = request->GetRect();
            const wxPoint grip(requestRect.GetRight() + 3, requestRect.y + 30);
            wxYieldIfNeeded();
            {
                std::vector<std::unique_ptr<WindowActivity>> untouched;
                for (auto* native : request->GetManager()->GetChildren()) {
                    if (dynamic_cast<wxTextCtrl*>(native)) {
                        native->Update();
                        auto activity = std::make_unique<WindowActivity>(native);
                        native->Refresh(); native->Update();
#ifndef __WXOSX__
                        // Cocoa native text fields do not reliably expose synchronous
                        // wx paint events. Their resize checks still run below.
                        check(activity->paints > 0, "native edit paint observer is active");
#endif
                        const auto originalSize = native->GetSize();
                        native->SetSize(originalSize + wxSize(1, 0));
                        native->SetSize(originalSize);
                        check(activity->sizes > 0, "native edit size observer is active");
                        activity->sizes = activity->paints = activity->erases = 0;
                        untouched.push_back(std::move(activity));
                    }
                }
                check(untouched.size() >= 5, "observe all unrelated single-line editors");
                splitter->OnButtonDown(grip);
                for (int delta = 5; delta <= 40; delta += 5) {
                    splitter->OnMouseMove(grip + wxPoint(delta,0));
                    wxYieldIfNeeded();
                    request->GetManager()->Update();
                }
                splitter->OnButtonUp(grip + wxPoint(40,0));
                wxYieldIfNeeded();
                for (const auto& activity : untouched) {
                    check(activity->sizes == 0, "split drag must not resize unrelated edits");
#ifndef __WXOSX__
                    check(activity->paints == 0 && activity->erases == 0,
                          "split drag must not repaint unrelated edits");
#endif
                }
            }
            check(request->GetWidth() == originalWidth + 40, "divider remains draggable");
            request->SetFixedWidth(0); window_->RefreshLayout();
            wxYieldIfNeeded();
            {
                auto* workspace = window_->Require<wxui::HorizontalLayout>("workspace");
                auto* navigation = window_->Require<wxui::VerticalLayout>("navigation");
                const auto navigationRect = navigation->GetRect();
                const wxPoint outerGrip(navigationRect.GetRight() + 3, navigationRect.y + 30);
                std::vector<std::unique_ptr<WindowActivity>> toolbar;
                for (auto* native : request->GetManager()->GetChildren()) {
                    if (dynamic_cast<wxTextCtrl*>(native) && native->GetRect().GetBottom() < workspace->GetRect().y)
                        toolbar.push_back(std::make_unique<WindowActivity>(native));
                }
                check(toolbar.size() == 4, "observe toolbar input fields");
                workspace->OnButtonDown(outerGrip);
                for (int delta = 5; delta <= 30; delta += 5) {
                    workspace->OnMouseMove(outerGrip + wxPoint(delta,0));
                    wxYieldIfNeeded();
                    request->GetManager()->Update();
                }
                workspace->OnButtonUp(outerGrip + wxPoint(30,0));
                check(navigation->GetWidth() == navigationRect.width + 30, "navigation divider remains draggable");
                for (const auto& activity : toolbar) {
                    check(activity->sizes == 0, "navigation split drag must not resize toolbar inputs");
#ifndef __WXOSX__
                    check(activity->paints == 0 && activity->erases == 0,
                          "navigation split drag must not repaint toolbar inputs");
#endif
                }
                navigation->SetFixedWidth(navigationRect.width);
            }
            window_->RefreshLayout();
            request->SetValueUtf8("请求😀");
            request->AppendBounded("中😀文", 4);
            check(request->GetValueUtf8() == "😀中😀文", "bounded Unicode append");
            request->AppendBounded("清空", 0);
            check(request->GetValueUtf8().empty(), "zero text capacity");
            response->SetJson(R"({"中文":42})");
            check(response->GetRawJson() == R"({"中文":42})", "JSON round trip");

            auto* edit = window_->Require<wxui::Edit>("search");
            int changes = 0;
            edit->Bind("valuechanged", [&changes](const wxui::NotifyEvent&) { ++changes; });
            edit->SetValueUtf8("中文 API");
            check(edit->GetValueUtf8() == "中文 API" && changes > 0, "edit notification");
            edit->Unbind("valuechanged");
            edit->Clear();
            check(edit->GetValueUtf8().empty(), "edit clear");

            auto* tree = window_->Require<wxui::TreeView>("apiTree");
            auto group = std::make_shared<wxui::TreeNode>();
            auto child = std::make_shared<wxui::TreeNode>();
            group->SetText("组"); child->SetText("操作");
            group->AddTreeChild(child); tree->AddRoot(group);
            int selections = 0;
            tree->Bind("itemselect", [&selections](const wxui::NotifyEvent&) { ++selections; });
            tree->SelectItem(1);
            check(child->GetParent() == tree, "tree node ownership for focus/scrolling");
            tree->RemoveAllRoots();
            auto other = std::make_shared<wxui::TreeNode>();
            other->AddTreeChild(std::make_shared<wxui::TreeNode>());
            tree->AddRoot(other); tree->SelectItem(1);
            check(selections == 2, "selection after filtering at the same index");
            other->SetExpanded(false);
            check(tree->GetCurSel() == -1, "collapsed selection cannot target a different node");
            tree->OnKeyDown(WXK_HOME);
            tree->OnKeyDown(WXK_RIGHT);
            check(other->IsExpanded(), "keyboard expand");
            tree->OnKeyDown(WXK_RIGHT);
            check(tree->GetCurSel() == 1, "keyboard select child");
            tree->GetVisibleNode(1)->OnKeyDown(WXK_LEFT);
            check(tree->GetCurSel() == 0, "keyboard select parent through focused node");
            tree->Unbind("itemselect");

            auto* tabs = window_->Require<wxui::TabLayout>("records");
            auto* events = window_->Require<wxui::RichEdit>("events");
            auto* history = window_->Require<wxui::RichEdit>("history");
            events->SetValueUtf8("events marker"); history->SetValueUtf8("history marker");
            tabs->SelectItem("history"); window_->RefreshLayout();
            check(!events->IsEffectivelyVisible() && history->IsEffectivelyVisible(), "tab selection");
            bool historyShown = false, eventsHidden = false;
            for (auto* native : history->GetManager()->GetChildren()) {
                auto* text = dynamic_cast<wxui::TextEditor*>(native);
                if (!text) continue;
                if (text->GetValue() == "history marker") historyShown = text->IsShown();
                if (text->GetValue() == "events marker") eventsHidden = !text->IsShown();
            }
            check(historyShown && eventsHidden, "native editors follow logical tab visibility");
            wxui::TextEditor* historyEditor = nullptr;
            for (auto* native : history->GetManager()->GetChildren()) {
                auto* text = dynamic_cast<wxui::TextEditor*>(native);
                if (text && text->GetValue() == "history marker") historyEditor = text;
            }
            check(historyEditor && historyEditor->Editor()->GetReadOnly(), "history remains read-only");
            check(!historyEditor->Editor()->GetUseVerticalScrollBar() &&
                  !historyEditor->Editor()->GetUseHorizontalScrollBar(), "platform scrollbars disabled");
            history->SetValueUtf8(std::string(2000, 'x') + std::string(120, '\n'));
            historyEditor->UpdateScrollbars();
            historyEditor->Editor()->SetFirstVisibleLine(20);
            historyEditor->UpdateScrollbars();
            check(historyEditor->Editor()->GetFirstVisibleLine() == 20, "editor vertical scrolling");
            {
                WindowActivity viewport(historyEditor->Editor());
                for (int i = 0; i < 20; ++i) historyEditor->UpdateScrollbars();
                check(viewport.sizes == 0, "stable scrollbar updates must not resize the viewport");
                check(historyEditor->Editor()->GetFirstVisibleLine() == 20,
                      "stable scrollbar updates retain scroll position");
            }
            history->Clear(); historyEditor->UpdateScrollbars();
            int visibleBars = 0;
            for (auto* child : historyEditor->GetChildren()) {
                if (child != historyEditor->Editor() && child->IsShown()) ++visibleBars;
            }
            check(visibleBars == 0, "empty editor hides both scrollbars");
            auto* password = new wxui::TextEditor(history->GetManager(), wxID_ANY,
                "secret", {}, {100,30}, wxTE_PASSWORD);
            check(password->GetValue() == "secret", "password value round trip");
            auto* nativePassword = dynamic_cast<wxTextCtrl*>(password->GetChildren().GetFirst()->GetData());
            check(nativePassword && nativePassword->HasFlag(wxTE_PASSWORD), "password remains masked");
            password->Clear(); password->Destroy();

            // Destruction cancels queued calls and invalidates senders retained by producers.
            bool abandoned = false;
            wxui::UiPost stale;
            {
                wxui::DesktopWindow temporary({"temporary"}, "<Window/>");
                stale = temporary.Poster();
                stale([&abandoned] { abandoned = true; });
            }
            check(!stale([] {}), "sender rejected after owner destruction");
            check(!abandoned, "queued callback abandoned");

            int closes = 0;
            window_->OnClose([&closes](bool canDefer) { check(canDefer, "normal close is vetoable"); ++closes; return false; });
            window_->RequestClose();
            check(closes == 1 && window_->FindControl("request"), "deferred close keeps UI alive");
            window_->OnClose({});
            sender_([this] { ++failures_; });
            window_->FinishClose();
            check(!sender_([] {}), "sender rejected after close");
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n'; ++failures_;
            window_->OnClose({});
            window_->FinishClose();
        }
    }
    int OnRun() override {
        wxui::Application::OnRun();
        return failures_ || delivered_ != 1 ? 1 : 0;
    }
    int OnAppExit() override { window_.reset(); return 0; }
};
}
WXUI_IMPLEMENT_APPLICATION(DesktopTest);
