#include <libwxui.hpp>
#include <libwxui/appearance.hpp>
#include <wx/dirdlg.h>
#include <wx/display.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>
#include <wx/statusbr.h>
#include <wx/weakref.h>
#include <mutex>
#include <algorithm>

namespace wxui {

	struct DesktopWindow::Impl {
		struct Dispatch {
			std::mutex mutex;
			wxEvtHandler* target = nullptr;
		};
		wxEvtHandler events;
		std::shared_ptr<Dispatch> dispatch = std::make_shared<Dispatch>();
		wxWeakRef<wxFrame> frame;
		UIManager* manager = nullptr;
		std::function<bool(bool)> onClose;
		bool closing = false;

		Impl() {
			dispatch->target = &events;
		}
		void Cancel() {
			{
				std::lock_guard lock(dispatch->mutex);
				dispatch->target = nullptr;
			}
			if (wxTheApp)
				wxTheApp->RemovePendingEventHandler(&events);
			events.DeletePendingEvents();
		}
		~Impl() {
			Cancel();
			// The native handler captures this Impl; remove it before deferred destruction.
			if (frame) {
				frame->Unbind(wxEVT_CLOSE_WINDOW, &Impl::Close, this);
				frame->Destroy();
			}
		}
		void Close(wxCloseEvent& event) {
			if (closing) {
				if (event.CanVeto())
					event.Veto();
				return;
			}
			if (onClose && !onClose(event.CanVeto()) && event.CanVeto()) {
				event.Veto();
				return;
			}
			closing = true;
			Cancel();
			frame->Destroy();
		}
	};

	DesktopWindow::DesktopWindow(const DesktopWindowSpec& spec, const std::string& xml,
	                             DesktopResourceLoader resources)
	    : impl_(std::make_unique<Impl>()) {
		FrameSpec native;
		native.title = spec.title;
		native.size = {spec.size.width, spec.size.height};
		native.statusBar.enabled = spec.statusFields > 0;
		native.statusBar.fields = spec.statusFields;
		auto* frame = new SdiFrame(native);
        if (spec.standardMenu) {
            auto* fileMenu = new wxMenu;
            fileMenu->Append(wxID_EXIT, "Quit\tCtrl+Q");
            auto* menuBar = new wxMenuBar;
            menuBar->Append(fileMenu, "File");
            frame->SetMenuBar(menuBar);
        }
#ifdef __WXOSX__
        else {
            wxMenuBar::SetAutoWindowMenu(false);
            frame->SetMenuBar(new wxMenuBar);
        }
#endif
#ifdef __WXOSX__
        // The green window control should enter a native full-screen Space.
        frame->EnableFullScreenView(true);
#endif
        wxAcceleratorEntry quit;
        quit.Set(wxACCEL_CMD, 'Q', wxID_EXIT);
        frame->SetAcceleratorTable(wxAcceleratorTable(1, &quit));
        frame->Bind(wxEVT_MENU, [frame](wxCommandEvent&) { frame->Close(); }, wxID_EXIT);
		frame->SetFont(InterfaceFont());
		if (frame->GetStatusBar())
			frame->GetStatusBar()->SetFont(InterfaceFont());
		impl_->frame = frame;
		frame->SetMinSize({spec.minimumSize.width, spec.minimumSize.height});
		if (!frame->LoadContentXml(xml, std::move(resources)))
			throw std::runtime_error("Invalid window UI XML");
		impl_->manager = frame->GetUIManager();
		frame->Bind(wxEVT_CLOSE_WINDOW, &Impl::Close, impl_.get());
	}

	DesktopWindow::~DesktopWindow() = default;
	Control* DesktopWindow::FindControl(const std::string& name) const {
		return impl_->frame && !impl_->closing ? impl_->manager->FindControl(name) : nullptr;
	}
	void DesktopWindow::Present() {
		if (impl_->frame) {
            // Initial bounds are outer-window bounds. Respect the selected display's
            // work area (menu bar/taskbar/Dock), without shrinking native full-screen.
            auto* frame = impl_->frame.get();
            if (!frame->IsFullScreen() && !frame->IsMaximized() && wxDisplay::GetCount()) {
                int display = wxDisplay::GetFromWindow(frame);
                if (display == wxNOT_FOUND) display = 0;
                const auto work = wxDisplay(display).GetClientArea();
                auto minimum = frame->GetMinSize();
                minimum.x = std::min(minimum.x, work.width);
                minimum.y = std::min(minimum.y, work.height);
                frame->SetMinSize(minimum);
                auto bounds = frame->GetRect();
                if (bounds.width > work.width || bounds.height > work.height) bounds = work;
                else {
                    bounds.x = std::clamp(bounds.x, work.x, work.GetRight() - bounds.width + 1);
                    bounds.y = std::clamp(bounds.y, work.y, work.GetBottom() - bounds.height + 1);
                }
                frame->SetSize(bounds);
            }
			impl_->frame->Show();
        }
	}
	void DesktopWindow::SetStatus(const std::string& text, int field) {
		if (impl_->frame)
			SetFrameStatusText(impl_->frame, Utf8ToWxString(text), field);
	}
	Extent DesktopWindow::ClientExtent() const {
		if (!impl_->frame)
			return {};
		const auto size = impl_->frame->GetClientSize();
		return {size.x, size.y};
	}
	void DesktopWindow::SetTitle(const std::string& text) {
		if (impl_->frame) impl_->frame->SetTitle(Utf8ToWxString(text));
	}
	void DesktopWindow::LoadLanguageXml(const std::string& language, const std::string& xml) {
		impl_->manager->LoadLanguageXml(language, xml);
	}
	void DesktopWindow::LoadLanguageResource(const std::string& language, const std::string& resource) {
		std::string xml;
		if (!impl_->manager->LoadResourceBytes(resource, &xml))
			throw std::runtime_error("Missing language resource: " + resource);
		LoadLanguageXml(language, xml);
	}
	void DesktopWindow::SetLanguage(const std::string& language) {
		impl_->manager->SetLanguage(language);
	}
	void DesktopWindow::SetFallbackLanguage(const std::string& language) {
		impl_->manager->SetFallbackLanguage(language);
	}
	const std::string& DesktopWindow::GetLanguage() const {
		return impl_->manager->GetLocalization().GetLanguage();
	}
	std::string DesktopWindow::Translate(const std::string& key, const std::string& fallback) const {
		return impl_->manager->Translate(key, fallback);
	}
	void DesktopWindow::RefreshLayout() {
		if (impl_->frame && !impl_->closing)
			impl_->manager->ForceLayout();
	}
	UiPost DesktopWindow::Poster() const {
		return [state = impl_->dispatch](std::function<void()> callback) {
			std::lock_guard lock(state->mutex);
			if (!state->target)
				return false;
			state->target->CallAfter(std::move(callback));
			return true;
		};
	}
	void DesktopWindow::OnClose(std::function<bool(bool)> callback) {
		impl_->onClose = std::move(callback);
	}
	void DesktopWindow::RequestClose() {
		if (impl_->frame)
			impl_->frame->Close();
	}
	void DesktopWindow::FinishClose() {
		impl_->closing = true;
		impl_->Cancel();
		if (impl_->frame)
			impl_->frame->Destroy();
	}
	std::optional<std::string> DesktopWindow::OpenFile(const std::string& title, const std::string& filter) {
		wxFileDialog dialog(impl_->frame, Utf8ToWxString(title), {}, {}, Utf8ToWxString(filter),
		                    wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dialog.ShowModal() != wxID_OK)
			return std::nullopt;
		return WxStringToUtf8(dialog.GetPath());
	}
	std::optional<std::string> DesktopWindow::SaveFile(const std::string& title,
	                                                   const std::string& filename, const std::string& filter) {
		wxFileDialog dialog(impl_->frame, Utf8ToWxString(title), {}, Utf8ToWxString(filename),
		                    Utf8ToWxString(filter), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (dialog.ShowModal() != wxID_OK)
			return std::nullopt;
		return WxStringToUtf8(dialog.GetPath());
	}
	std::optional<std::string> DesktopWindow::ChooseDirectory(const std::string& title) {
		wxDirDialog dialog(impl_->frame, Utf8ToWxString(title));
		if (dialog.ShowModal() != wxID_OK)
			return std::nullopt;
		return WxStringToUtf8(dialog.GetPath());
	}
	bool DesktopWindow::Confirm(const std::string& title, const std::string& message) {
		return wxMessageBox(Utf8ToWxString(message), Utf8ToWxString(title),
		                    wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, impl_->frame) == wxYES;
	}
	void DesktopWindow::Error(const std::string& title, const std::string& message) {
		wxMessageBox(Utf8ToWxString(message), Utf8ToWxString(title), wxOK | wxICON_ERROR, impl_->frame);
	}
	void DesktopWindow::ShowText(const std::string& title, std::string_view text) {
		wxDialog dialog(impl_->frame, wxID_ANY, Utf8ToWxString(title), wxDefaultPosition,
		                {900, 700}, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
		dialog.SetFont(InterfaceFont());
		auto* manager = new UIManager(&dialog);
		if (!manager->LoadFromString(std::string(R"(<Window><VerticalLayout inset="10,10,10,10"><RichEdit name="text" readonly="true" wanttab="false"/></VerticalLayout></Window>)")))
			throw std::runtime_error("Invalid text dialog XML");
		auto* edit = dynamic_cast<RichEdit*>(manager->FindControl("text"));
		edit->SetValue(text);
		auto* sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(manager, 1, wxEXPAND);
		dialog.SetSizer(sizer);
		dialog.CentreOnParent();
		dialog.ShowModal();
	}
	bool DesktopWindow::ShowDialog(const std::string& title, const std::string& xml,
	                              Extent size, std::function<void(Control&)> initialize) {
		wxDialog dialog(impl_->frame, wxID_ANY, Utf8ToWxString(title), wxDefaultPosition,
		                {size.width, size.height}, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
		dialog.SetFont(impl_->manager->GetUIFont());
		auto* manager = new UIManager(&dialog);
		manager->SetUIFont(impl_->manager->GetUIFont());
		if (!manager->LoadFromString(xml)) throw std::runtime_error("Invalid dialog XML");
		if (auto* close = manager->FindControl("close"))
			close->Bind("click", [&dialog](const auto&) { dialog.EndModal(wxID_CANCEL); });
		if (auto* accept = manager->FindControl("accept"))
			accept->Bind("click", [&dialog](const auto&) { dialog.EndModal(wxID_OK); });
		if (initialize) initialize(*manager->GetRoot());
		auto* sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(manager, 1, wxEXPAND);
		dialog.SetSizer(sizer);
		dialog.CentreOnParent();
		return dialog.ShowModal() == wxID_OK;
	}
	std::string ExecutablePath() {
		return WxStringToUtf8(wxStandardPaths::Get().GetExecutablePath());
}
	std::string HostName() {
		return WxStringToUtf8(wxGetHostName());
	}
	void ShowError(const std::string& title, const std::string& message) {
		wxMessageBox(Utf8ToWxString(message), Utf8ToWxString(title), wxOK | wxICON_ERROR);
	}

} // namespace wxui
