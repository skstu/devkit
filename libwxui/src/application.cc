#include <libwxui.hpp>

#include <wx/dcclient.h>
#include <wx/sizer.h>
#include <wx/statusbr.h>
#include <wx/sstream.h>
#include <wx/xml/xml.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wxui {
	namespace {

		constexpr int kDefaultStatusBarHeight = 24;

		class ColoredStatusBar : public wxStatusBar {
		public:
			ColoredStatusBar(wxWindow* parent, long style)
			    : wxStatusBar(parent, wxID_ANY, style) {
				SetMinHeight(kDefaultStatusBarHeight);
				SetMinSize(wxSize(-1, kDefaultStatusBarHeight));
				Bind(wxEVT_PAINT, &ColoredStatusBar::OnPaint, this);
				Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
			}

		private:
			void OnPaint(wxPaintEvent&) {
				wxPaintDC dc(this);
				const wxSize size = GetClientSize();
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.SetBrush(wxBrush(GetBackgroundColour()));
				dc.DrawRectangle(0, 0, size.x, size.y);

				dc.SetFont(GetFont());
				dc.SetTextForeground(GetForegroundColour());
				for (int i = 0; i < GetFieldsCount(); ++i) {
					wxRect rect;
					if (!GetFieldRect(i, rect))
						continue;

					const wxString text = GetStatusText(i);
					wxCoord textWidth = 0;
					wxCoord textHeight = 0;
					dc.GetTextExtent(text, &textWidth, &textHeight);
					const int x = rect.x + 4;
					const int y = rect.y + std::max(0, (rect.height - textHeight) / 2);
					dc.DrawText(text, x, y);
				}
			}
		};

		std::unordered_map<wxFrame*, wxString> gFrameStatusText;
		std::unordered_map<wxFrame*, wxColour> gFrameStatusTextColor;

		wxStatusBar* FrameStatusBar(wxFrame* frame) {
			return frame ? frame->GetStatusBar() : nullptr;
		}

		void RefreshFrameStatusBarLayout(wxFrame* frame) {
			if (!frame)
				return;
			auto* statusBar = frame->GetStatusBar();
			if (!statusBar)
				return;
			statusBar->Show(true);
			statusBar->Refresh();
			frame->Layout();
			frame->SendSizeEvent();
		}

		wxString DesiredFrameStatusText(wxFrame* frame) {
			if (!frame)
				return {};
			if (auto it = gFrameStatusText.find(frame); it != gFrameStatusText.end()) {
				return it->second;
			}
			if (auto* statusBar = FrameStatusBar(frame)) {
				return statusBar->GetStatusText(0);
			}
			return {};
		}

		void ApplyFrameStatusTextColor(wxFrame* frame) {
			if (!frame)
				return;
			auto* statusBar = FrameStatusBar(frame);
			if (!statusBar)
				return;
			auto it = gFrameStatusTextColor.find(frame);
			if (it == gFrameStatusTextColor.end() || !it->second.IsOk())
				return;
			statusBar->SetOwnForegroundColour(it->second);
			statusBar->SetForegroundColour(it->second);
		}

		wxStatusBar* CreateStatusBarForSpec(wxFrame* frame, const StatusBarSpec& spec,
		                                    long style) {
			if (spec.bkColor.IsOk() || spec.textColor.IsOk()) {
				auto* statusBar = new ColoredStatusBar(frame, style);
				statusBar->SetFieldsCount(spec.fields);
				frame->SetStatusBar(statusBar);
				return statusBar;
			}
			return frame->CreateStatusBar(spec.fields, style);
		}

		std::string Attr(wxXmlNode* node, const char* name, std::string fallback = {}) {
			if (!node)
				return fallback;
			wxString value;
			return node->GetAttribute(Utf8ToWxString(name), &value)
			           ? WxStringToUtf8(value)
			           : fallback;
		}

		std::string PlatformFontFace(wxXmlNode* node) {
			std::string fallback = Attr(node, "fontface", Attr(node, "fontFace"));
#if defined(_WIN32)
			return Attr(node, "fontfacewindows",
			            Attr(node, "fontFaceWindows",
			                 Attr(node, "fontfacewin",
			                      Attr(node, "fontFaceWin", fallback))));
#elif defined(__APPLE__)
			return Attr(node, "fontfacemacos",
			            Attr(node, "fontFaceMacos",
			                 Attr(node, "fontfacemac",
			                      Attr(node, "fontFaceMac", fallback))));
#elif defined(__linux__)
			return Attr(node, "fontfacelinux",
			            Attr(node, "fontFaceLinux",
			                 Attr(node, "fontfaceunix",
			                      Attr(node, "fontFaceUnix", fallback))));
#else
			return fallback;
#endif
		}

		int AttrIntValue(wxXmlNode* node, const char* name, int fallback) {
			return ParseINT(Attr(node, name), fallback);
		}

		bool AttrBoolValue(wxXmlNode* node, const char* name, bool fallback) {
			return ParseBOOL(Attr(node, name), fallback);
		}

		int ParseMenuId(const std::string& value) {
			const std::string normalized = NormalizeXmlIdentifier(value);
			if (normalized == "exit" || normalized == "quit")
				return wxID_EXIT;
			if (normalized == "about")
				return wxID_ABOUT;
			if (normalized == "preferences" || normalized == "settings")
				return wxID_PREFERENCES;
			return ParseINT(value, wxID_ANY);
		}

		FrameType ParseFrameType(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(),
			               [](unsigned char c) { return char(std::tolower(c)); });
			return value == "mdi" ? FrameType::Mdi : FrameType::Sdi;
		}

		wxXmlNode* FirstElement(wxXmlNode* parent, std::string_view name) {
			if (!parent)
				return nullptr;
			const std::string normalizedName = NormalizeXmlIdentifier(name);
			for (auto* child = parent->GetChildren(); child; child = child->GetNext()) {
				if (child->GetType() != wxXML_ELEMENT_NODE)
					continue;
				if (NormalizeXmlIdentifier(WxStringToUtf8(child->GetName())) == normalizedName)
					return child;
			}
			return nullptr;
		}

		void ParseChild(wxXmlNode* node, ChildSpec* out) {
			if (!node || !out)
				return;
			out->name = Attr(node, "name");
			out->title = Attr(node, "title");
			out->xml = Attr(node, "xml", Attr(node, "ui"));
		}

		void ParseDialog(wxXmlNode* node, DialogSpec* out) {
			if (!node || !out)
				return;
			out->name = Attr(node, "name");
			out->title = Attr(node, "title", out->name);
			out->xml = Attr(node, "xml", Attr(node, "ui"));
			out->size = wxSize(AttrIntValue(node, "width", out->size.x),
			                   AttrIntValue(node, "height", out->size.y));
			if (auto size = Attr(node, "size"); !size.empty()) {
				if (NormalizeXmlIdentifier(size) == "auto") {
					out->autoSize = true;
				}
				else {
					out->size = ParseSIZE(size, out->size);
				}
			}
			out->autoSize = AttrBoolValue(node, "autosize", out->autoSize);
			out->center = AttrBoolValue(node, "center", out->center);
		}

		void ParseMenuItem(wxXmlNode* node, MenuItemSpec* out) {
			if (!node || !out)
				return;
			out->separator = NormalizeXmlIdentifier(WxStringToUtf8(node->GetName())) == "separator" ||
			                 AttrBoolValue(node, "separator", false);
			out->name = Attr(node, "name");
			out->text = Attr(node, "text", Attr(node, "label"));
			out->help = Attr(node, "help");
			out->id = ParseMenuId(Attr(node, "id", out->name));
			out->checkable = AttrBoolValue(node, "checkable",
			                               AttrBoolValue(node, "check", out->checkable));
			out->enabled = AttrBoolValue(node, "enabled",
			                             AttrBoolValue(node, "enable", out->enabled));
			out->items.clear();
			for (auto* child = node->GetChildren(); child; child = child->GetNext()) {
				if (child->GetType() != wxXML_ELEMENT_NODE)
					continue;
				const std::string tag = NormalizeXmlIdentifier(WxStringToUtf8(child->GetName()));
				if (tag != "item" && tag != "menuitem" && tag != "separator")
					continue;
				MenuItemSpec item;
				ParseMenuItem(child, &item);
				out->items.push_back(std::move(item));
			}
		}

		void ParseMenu(wxXmlNode* node, MenuSpec* out) {
			if (!node || !out)
				return;
			out->name = Attr(node, "name");
			out->text = Attr(node, "text", Attr(node, "label", out->name));
			out->items.clear();
			for (auto* child = node->GetChildren(); child; child = child->GetNext()) {
				if (child->GetType() != wxXML_ELEMENT_NODE)
					continue;
				const std::string tag = NormalizeXmlIdentifier(WxStringToUtf8(child->GetName()));
				if (tag != "item" && tag != "menuitem" && tag != "separator")
					continue;
				MenuItemSpec item;
				ParseMenuItem(child, &item);
				out->items.push_back(std::move(item));
			}
		}

		void ParseStatusBar(wxXmlNode* node, StatusBarSpec* out) {
			if (!node || !out)
				return;
			out->enabled = AttrBoolValue(node, "enabled",
			                             AttrBoolValue(node, "visible", true));
			out->fields = std::max(1, AttrIntValue(node, "fields", out->fields));
			out->text = Attr(node, "text", Attr(node, "label", out->text));
			out->bkColor = ParseDWORD(Attr(node, "bkcolor", Attr(node, "bkColor")),
			                          out->bkColor);
			out->textColor = ParseDWORD(Attr(node, "textcolor", Attr(node, "textColor")),
			                            out->textColor);
			out->flat = AttrBoolValue(node, "flat", out->flat);
			out->sizeGrip = AttrBoolValue(node, "sizegrip",
			                              AttrBoolValue(node, "sizeGrip", out->sizeGrip));
			const std::string border = NormalizeXmlIdentifier(Attr(node, "border"));
			if (!border.empty()) {
				out->border = border != "none" && border != "false" && border != "0";
			}
			else {
				out->border = AttrBoolValue(node, "border", out->border);
			}
		}

		void ApplyStatusBarSpec(wxFrame* frame, const StatusBarSpec& spec) {
			if (!frame || !spec.enabled)
				return;

			long style = wxSTB_DEFAULT_STYLE;
			if (!spec.sizeGrip)
				style &= ~wxSTB_SIZEGRIP;
			if (!spec.border)
				style |= wxBORDER_NONE;

			auto* statusBar = CreateStatusBarForSpec(frame, spec, style);
			if (!statusBar)
				return;

			statusBar->SetMinHeight(kDefaultStatusBarHeight);
			statusBar->SetMinSize(wxSize(-1, kDefaultStatusBarHeight));
			if (spec.flat) {
				std::vector<int> styles(static_cast<size_t>(spec.fields), wxSB_FLAT);
				statusBar->SetStatusStyles(spec.fields, styles.data());
			}
			if (spec.bkColor.IsOk()) {
				statusBar->SetOwnBackgroundColour(spec.bkColor);
				statusBar->SetBackgroundColour(spec.bkColor);
			}
			if (spec.textColor.IsOk()) {
				gFrameStatusTextColor[frame] = spec.textColor;
				ApplyFrameStatusTextColor(frame);
			}
			if (!spec.text.empty()) {
				SetFrameStatusText(frame, Utf8ToWxString(spec.text));
			}
			statusBar->Refresh();
			RefreshFrameStatusBarLayout(frame);
			frame->Bind(wxEVT_SHOW, [frame](wxShowEvent& event) {
				event.Skip();
				if (event.IsShown()) {
					frame->CallAfter([frame] {
						RefreshFrameStatusBarLayout(frame);
					});
				}
			});
			frame->CallAfter([frame] {
				RefreshFrameStatusBarLayout(frame);
			});

			if (!spec.text.empty()) {
				auto restoreText = [frame](wxMenuEvent& event) {
					const wxString text = DesiredFrameStatusText(frame);
					event.Skip();
					frame->CallAfter([frame, text] {
						if (FrameStatusBar(frame)) {
							ApplyFrameStatusTextColor(frame);
							SetFrameStatusText(frame, text);
						}
					});
				};
				frame->Bind(wxEVT_MENU_OPEN, restoreText);
				frame->Bind(wxEVT_MENU_CLOSE, restoreText);
				frame->Bind(wxEVT_MENU_HIGHLIGHT, restoreText);
				frame->Bind(wxEVT_DESTROY, [](wxWindowDestroyEvent& event) {
					if (auto* frame = dynamic_cast<wxFrame*>(event.GetEventObject())) {
						gFrameStatusText.erase(frame);
						gFrameStatusTextColor.erase(frame);
					}
					event.Skip();
				});
			}
		}

		void ParseFrame(wxXmlNode* node, FrameSpec* out) {
			if (!node || !out)
				return;
			out->type = ParseFrameType(Attr(node, "type", "sdi"));
			out->name = Attr(node, "name");
			out->title = Attr(node, "title", out->name);
			out->xml = Attr(node, "xml", Attr(node, "ui"));
			out->size = wxSize(AttrIntValue(node, "width", out->size.x),
			                   AttrIntValue(node, "height", out->size.y));
			if (auto size = Attr(node, "size"); !size.empty()) {
				if (NormalizeXmlIdentifier(size) == "auto") {
					out->autoSize = true;
				}
				else {
					out->size = ParseSIZE(size, out->size);
				}
			}
			out->autoSize = AttrBoolValue(node, "autosize", out->autoSize);
			out->center = AttrBoolValue(node, "center", out->center);
			out->statusBar = StatusBarSpec{};

			out->menus.clear();
			for (auto* child = node->GetChildren(); child; child = child->GetNext()) {
				if (child->GetType() != wxXML_ELEMENT_NODE)
					continue;
				const std::string tag = NormalizeXmlIdentifier(WxStringToUtf8(child->GetName()));
				if (tag == "statusbar" || tag == "status") {
					ParseStatusBar(child, &out->statusBar);
					continue;
				}
				if (tag != "menu")
					continue;
				MenuSpec spec;
				ParseMenu(child, &spec);
				out->menus.push_back(std::move(spec));
			}

			out->children.clear();
			for (auto* child = node->GetChildren(); child; child = child->GetNext()) {
				if (child->GetType() != wxXML_ELEMENT_NODE)
					continue;
				if (NormalizeXmlIdentifier(WxStringToUtf8(child->GetName())) != "child")
					continue;
				ChildSpec spec;
				ParseChild(child, &spec);
				out->children.push_back(std::move(spec));
			}
		}

		bool LoadAppSpecFromDocument(wxXmlDocument& doc, AppSpec* out,
		                             std::string* error) {
			if (!out) {
				if (error)
					*error = "AppSpec output is null.";
				return false;
			}

			wxXmlNode* root = doc.GetRoot();
			if (!root) {
				if (error)
					*error = "Application shell XML has no root node.";
				return false;
			}

			wxXmlNode* appNode = root;
			if (NormalizeXmlIdentifier(WxStringToUtf8(root->GetName())) != "app") {
				if (error)
					*error = "Application shell XML root must be <app>.";
				return false;
			}

			out->name = Attr(appNode, "name", out->name);
			out->fontFace = PlatformFontFace(appNode);
			out->dialogs.clear();
			for (auto* child = appNode->GetChildren(); child; child = child->GetNext()) {
				if (child->GetType() != wxXML_ELEMENT_NODE)
					continue;
				if (NormalizeXmlIdentifier(WxStringToUtf8(child->GetName())) != "dialog")
					continue;
				DialogSpec dialog;
				ParseDialog(child, &dialog);
				out->dialogs.push_back(std::move(dialog));
			}

			wxXmlNode* frameNode = nullptr;
			frameNode = FirstElement(appNode, "frame");
			if (!frameNode) {
				if (error)
					*error = "Application shell XML is missing <frame>.";
				return false;
			}

			ParseFrame(frameNode, &out->frame);
			if (out->frame.title.empty()) {
				out->frame.title = out->name;
			}
			return true;
		}

	} // namespace

	bool LoadAppSpecFromFile(const std::string& xmlPath, AppSpec* out,
	                         std::string* error) {
		wxXmlDocument doc;
		if (!doc.Load(Utf8ToWxString(xmlPath))) {
			if (error)
				*error = FormatString("Failed to load app XML: {}", xmlPath);
			return false;
		}
		return LoadAppSpecFromDocument(doc, out, error);
	}

	bool LoadAppSpecFromString(const std::string& xmlContent, AppSpec* out,
	                           std::string* error) {
		wxStringInputStream stream(Utf8ToWxString(xmlContent));
		wxXmlDocument doc;
		if (!doc.Load(stream)) {
			if (error)
				*error = "Failed to parse app XML.";
			return false;
		}
		return LoadAppSpecFromDocument(doc, out, error);
	}

	const DialogSpec* FindDialogSpec(const AppSpec& spec, std::string_view name) {
		for (const auto& dialog : spec.dialogs) {
			if (dialog.name == name)
				return &dialog;
		}
		return nullptr;
	}

	static wxMenu* BuildMenu(const std::vector<MenuItemSpec>& items,
	                         std::unordered_map<std::string, int>* ids) {
		auto* menu = new wxMenu();
		for (const auto& item : items) {
			if (item.separator) {
				menu->AppendSeparator();
				continue;
			}
			if (!item.items.empty()) {
				menu->AppendSubMenu(BuildMenu(item.items, ids),
				                    Utf8ToWxString(item.text),
				                    Utf8ToWxString(item.help));
				continue;
			}
			const int id = item.id == wxID_ANY ? wxWindow::NewControlId() : item.id;
			if (item.checkable) {
				menu->AppendCheckItem(id, Utf8ToWxString(item.text),
				                      Utf8ToWxString(item.help));
			}
			else {
				menu->Append(id, Utf8ToWxString(item.text),
				             Utf8ToWxString(item.help));
			}
			menu->Enable(id, item.enabled);
			if (ids && !item.name.empty()) {
				(*ids)[item.name] = id;
			}
		}
		return menu;
	}

	wxMenuBar* BuildMenuBar(const std::vector<MenuSpec>& menus,
	                        std::unordered_map<std::string, int>* ids) {
		auto* menuBar = new wxMenuBar();
		for (const auto& menuSpec : menus) {
			menuBar->Append(BuildMenu(menuSpec.items, ids),
			                Utf8ToWxString(menuSpec.text));
		}
		return menuBar;
	}

	std::string ShowPopupMenu(wxWindow* owner,
	                          const std::vector<MenuItemSpec>& items,
	                          const wxPoint& pos) {
		if (!owner)
			return {};

		std::unordered_map<std::string, int> ids;
		wxMenu* rawMenu = BuildMenu(items, &ids);
		const int selected = pos == wxDefaultPosition
		                         ? owner->GetPopupMenuSelectionFromUser(*rawMenu)
		                         : owner->GetPopupMenuSelectionFromUser(*rawMenu, pos);
		delete rawMenu;

		if (selected == wxID_NONE)
			return {};
		for (const auto& [name, id] : ids) {
			if (id == selected)
				return name;
		}
		return {};
	}

	void SetFrameStatusText(wxFrame* frame, const wxString& text, int field) {
		if (!frame)
			return;
		if (field == 0)
			gFrameStatusText[frame] = text;
		if (auto* statusBar = FrameStatusBar(frame)) {
			ApplyFrameStatusTextColor(frame);
			statusBar->SetStatusText(text, field);
			statusBar->Refresh();
		}
	}

	bool LoadUiContent(UIManager* manager, const std::string& resourceRoot,
	                   UIManager::ResourceLoader resourceLoader,
	                   const std::string& xmlPath, std::string* error) {
		if (!manager) {
			if (error)
				*error = "UIManager is null.";
			return false;
		}
		if (xmlPath.empty()) {
			if (error)
				*error = "UI content XML path is empty.";
			return false;
		}

		manager->SetResourceRoot(resourceRoot);
		manager->SetResourceLoader(std::move(resourceLoader));
		if (!manager->LoadFromResource(xmlPath)) {
			if (error)
				*error = FormatString("Failed to load UI XML: {}", xmlPath);
			return false;
		}
		return true;
	}

	bool Application::OnInit() {
		// Applications parse their UTF-8 Arguments() in OnAppInit. The default
		// wxApp parser rejects application-specific options before that hook runs.
		return OnAppInit();
	}

	int Application::OnExit() {
		const int appExit = OnAppExit();
		const int wxExit = wxApp::OnExit();
		return appExit != 0 ? appExit : wxExit;
	}

	bool Application::OnAppInit() {
		return true;
	}

	void Application::SetName(const std::string& name) {
		SetAppName(Utf8ToWxString(name));
	}

	std::vector<std::string> Application::Arguments() const {
		std::vector<std::string> result;
		for (int i = 0; i < argc; ++i)
			result.push_back(WxStringToUtf8(wxString(argv[i])));
		return result;
	}

	int Application::OnAppExit() {
		return 0;
	}

	void Application::SetResourceRoot(const std::string& resourceRoot) {
		resourceRoot_ = resourceRoot;
	}

	void Application::SetResourceLoader(ResourceLoader loader) {
		resourceLoader_ = std::move(loader);
	}

	bool Application::LoadShellFromFile(const std::string& xmlPath,
	                                    std::string* error) {
		return LoadAppSpecFromFile(xmlPath, &appSpec_, error);
	}

	bool Application::LoadShellFromResource(const std::string& xmlPath,
	                                        std::string* error) {
		std::string xml;
		if (resourceLoader_ && resourceLoader_(xmlPath, &xml)) {
			return LoadShellFromString(xml, error);
		}

		const std::string full =
		    resourceRoot_.empty() ? xmlPath : resourceRoot_ + "/" + xmlPath;
		return LoadShellFromFile(full, error);
	}

	bool Application::LoadShellFromString(const std::string& xmlContent,
	                                      std::string* error) {
		return LoadAppSpecFromString(xmlContent, &appSpec_, error);
	}

	bool Application::LoadContent(UIManager* manager, const std::string& xmlPath,
	                              std::string* error) const {
		return LoadUiContent(manager, resourceRoot_, resourceLoader_, xmlPath, error);
	}

	SdiFrame::SdiFrame(const FrameSpec& spec, wxWindow* parent, wxWindowID id)
	    : wxFrame(parent, id, Utf8ToWxString(spec.title), wxDefaultPosition,
	              spec.size) {
		if (!spec.menus.empty())
			SetMenuBar(BuildMenuBar(spec.menus));
		ApplyStatusBarSpec(this, spec.statusBar);
		if (spec.center)
			CentreOnScreen();
	}

	UIManager* SdiFrame::EnsureUiManager() {
		if (uiManager_)
			return uiManager_;
		ControlFactory::RegisterBuiltins();
		uiManager_ = new UIManager(this);
		auto* sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(uiManager_, 1, wxEXPAND);
		SetSizer(sizer);
		Layout();
		RefreshFrameStatusBarLayout(this);
		return uiManager_;
	}

	bool SdiFrame::LoadContent(const std::string& resourceRoot,
	                           const std::string& xmlPath) {
		auto* mgr = EnsureUiManager();
		mgr->SetResourceRoot(resourceRoot);
		return mgr->LoadFromFile(xmlPath);
	}

	bool SdiFrame::LoadContentXml(const std::string& xml, UIManager::ResourceLoader resources) {
		auto* manager = EnsureUiManager();
		manager->SetResourceLoader(std::move(resources));
		return manager->LoadFromString(xml);
	}

	MdiFrame::MdiFrame(const FrameSpec& spec, wxWindow* parent, wxWindowID id)
	    : wxMDIParentFrame(parent, id, Utf8ToWxString(spec.title),
	                       wxDefaultPosition, spec.size) {
		if (!spec.menus.empty())
			SetMenuBar(BuildMenuBar(spec.menus));
		ApplyStatusBarSpec(this, spec.statusBar);
		if (spec.center)
			CentreOnScreen();
	}

	UIManager* MdiFrame::EnsureUiManager() {
		if (uiManager_)
			return uiManager_;
		ControlFactory::RegisterBuiltins();
		contentFrame_ = new wxMDIChildFrame(this, wxID_ANY, GetTitle(),
		                                    wxDefaultPosition, GetClientSize());
		uiManager_ = new UIManager(contentFrame_);
		auto* sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(uiManager_, 1, wxEXPAND);
		contentFrame_->SetSizer(sizer);
		contentFrame_->Layout();
		contentFrame_->Show(true);
		return uiManager_;
	}

	bool MdiFrame::LoadContent(const std::string& resourceRoot,
	                           const std::string& xmlPath) {
		auto* mgr = EnsureUiManager();
		mgr->SetResourceRoot(resourceRoot);
		return mgr->LoadFromFile(xmlPath);
	}

	std::unique_ptr<FrameHost> CreateFrameHost(const FrameSpec& spec) {
		if (spec.type == FrameType::Mdi) {
			return std::make_unique<MdiFrame>(spec);
		}
		return std::make_unique<SdiFrame>(spec);
	}

	Dialog::Dialog(wxWindow* parent, const DialogSpec& spec, wxWindowID id,
	               long style)
	    : wxDialog(parent, id, Utf8ToWxString(spec.title), wxDefaultPosition,
	               spec.size, style) {
		if (spec.center)
			CentreOnScreen();
	}

	UIManager* Dialog::EnsureUiManager() {
		if (uiManager_)
			return uiManager_;
		ControlFactory::RegisterBuiltins();
		uiManager_ = new UIManager(this);
		auto* sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(uiManager_, 1, wxEXPAND);
		SetSizer(sizer);
		Layout();
		return uiManager_;
	}

	bool Dialog::LoadContent(const std::string& resourceRoot,
	                         const std::string& xmlPath) {
		auto* mgr = EnsureUiManager();
		mgr->SetResourceRoot(resourceRoot);
		return mgr->LoadFromFile(xmlPath);
	}

} // namespace wxui
