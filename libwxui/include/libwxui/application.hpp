#pragma once
/// libwxui application shell: XML-described wxApp / frame host.

#include "manager.hpp"

#include <wx/app.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/mdi.h>
#include <wx/menu.h>

#include <memory>
#include <unordered_map>

namespace wxui {

	enum class FrameType
	{
		Sdi,
		Mdi,
	};

	struct ChildSpec {
		std::string name;
		std::string title;
		std::string xml;
	};

	struct StatusBarSpec {
		bool enabled = false;
		int fields = 1;
		std::string text;
		wxColour bkColor;
		wxColour textColor;
		bool flat = true;
		bool sizeGrip = false;
		bool border = false;
	};

	struct MenuItemSpec {
		std::string name;
		std::string text;
		std::string help;
		std::vector<MenuItemSpec> items;
		int id = wxID_ANY;
		bool separator = false;
		bool checkable = false;
		bool enabled = true;
	};

	struct MenuSpec {
		std::string name;
		std::string text;
		std::vector<MenuItemSpec> items;
	};

	struct DialogSpec {
		std::string name;
		std::string title;
		std::string xml;
		wxSize size{640, 480};
		bool autoSize = false;
		bool center = true;
	};

	struct FrameSpec {
		FrameType type = FrameType::Sdi;
		std::string name;
		std::string title;
		std::string xml;
		wxSize size{0, 0};
		bool autoSize = false;
		bool center = true;
		StatusBarSpec statusBar;
		std::vector<MenuSpec> menus;
		std::vector<ChildSpec> children;
	};

	struct AppSpec {
		std::string name;
		std::string fontFace;
		FrameSpec frame;
		std::vector<DialogSpec> dialogs;
	};

	bool LoadAppSpecFromFile(const std::string& xmlPath, AppSpec* out,
	                         std::string* error = nullptr);
	bool LoadAppSpecFromString(const std::string& xmlContent, AppSpec* out,
	                           std::string* error = nullptr);
	const DialogSpec* FindDialogSpec(const AppSpec& spec, std::string_view name);
	wxMenuBar* BuildMenuBar(const std::vector<MenuSpec>& menus,
	                        std::unordered_map<std::string, int>* ids = nullptr);
	std::string ShowPopupMenu(wxWindow* owner,
	                          const std::vector<MenuItemSpec>& items,
	                          const wxPoint& pos = wxDefaultPosition);
	void SetFrameStatusText(wxFrame* frame, const wxString& text, int field = 0);
	bool LoadUiContent(UIManager* manager, const std::string& resourceRoot,
	                   UIManager::ResourceLoader resourceLoader,
	                   const std::string& xmlPath,
	                   std::string* error = nullptr);

	class Application : public wxApp {
	public:
		bool OnInit() override;
		int OnExit() override;
		void SetName(const std::string& name);
		std::vector<std::string> Arguments() const;

	protected:
		using ResourceLoader = UIManager::ResourceLoader;

		virtual bool OnAppInit();
		virtual int OnAppExit();

		void SetResourceRoot(const std::string& resourceRoot);
		[[nodiscard]] const std::string& GetResourceRoot() const {
			return resourceRoot_;
		}

		void SetResourceLoader(ResourceLoader loader);

		bool LoadShellFromFile(const std::string& xmlPath,
		                       std::string* error = nullptr);
		bool LoadShellFromResource(const std::string& xmlPath,
		                           std::string* error = nullptr);
		bool LoadShellFromString(const std::string& xmlContent,
		                         std::string* error = nullptr);

		bool LoadContent(UIManager* manager, const std::string& xmlPath,
		                 std::string* error = nullptr) const;

		[[nodiscard]] const AppSpec& GetShellSpec() const {
			return appSpec_;
		}
		[[nodiscard]] AppSpec& MutableShellSpec() {
			return appSpec_;
		}

	private:
		AppSpec appSpec_;
		std::string resourceRoot_;
		ResourceLoader resourceLoader_;
	};

	class FrameHost {
	public:
		virtual ~FrameHost() = default;
		virtual UIManager* GetUIManager() = 0;
		virtual bool LoadContent(const std::string& resourceRoot,
		                         const std::string& xmlPath) = 0;
	};

	class SdiFrame : public wxFrame, public FrameHost {
	public:
		explicit SdiFrame(const FrameSpec& spec, wxWindow* parent = nullptr,
		                  wxWindowID id = wxID_ANY);

		UIManager* GetUIManager() override {
			return uiManager_;
		}
		bool LoadContentXml(const std::string& xml, UIManager::ResourceLoader resources = {});
		bool LoadContent(const std::string& resourceRoot,
		                 const std::string& xmlPath) override;

	protected:
		UIManager* EnsureUiManager();

	private:
		UIManager* uiManager_ = nullptr;
	};

	class MdiFrame : public wxMDIParentFrame, public FrameHost {
	public:
		explicit MdiFrame(const FrameSpec& spec, wxWindow* parent = nullptr,
		                  wxWindowID id = wxID_ANY);

		UIManager* GetUIManager() override {
			return uiManager_;
		}
		bool LoadContent(const std::string& resourceRoot,
		                 const std::string& xmlPath) override;

	protected:
		UIManager* EnsureUiManager();

	private:
		UIManager* uiManager_ = nullptr;
		wxMDIChildFrame* contentFrame_ = nullptr;
	};

	class Dialog : public wxDialog {
	public:
		explicit Dialog(wxWindow* parent, const DialogSpec& spec,
		                wxWindowID id = wxID_ANY,
		                long style = wxDEFAULT_DIALOG_STYLE | wxCAPTION |
		                             wxCLOSE_BOX);

		UIManager* GetUIManager() {
			return uiManager_;
		}
		bool LoadContent(const std::string& resourceRoot, const std::string& xmlPath);

	protected:
		UIManager* EnsureUiManager();

	private:
		UIManager* uiManager_ = nullptr;
	};

	std::unique_ptr<FrameHost> CreateFrameHost(const FrameSpec& spec);

} // namespace wxui

// Native platform entry points stay inside libwxui.
#define WXUI_IMPLEMENT_APPLICATION(AppClass) wxIMPLEMENT_APP(AppClass)
