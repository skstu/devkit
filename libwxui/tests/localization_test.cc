#include <libwxui.hpp>
#include "assets.hpp"
#include <iostream>

namespace {
	void Check(bool value, const char* message) {
		if (!value)
			throw std::runtime_error(message);
	}
	template <class Action>
	void Reject(Action action, const char* message) {
		bool rejected = false;
		try {
			action();
		}
		catch (const std::exception&) {
			rejected = true;
		}
		Check(rejected, message);
	}

	class LocalizationTest : public wxui::Application {
		std::unique_ptr<wxui::DesktopWindow> window_;
		int failures_ = 0;
		bool OnAppInit() override {
			window_ = std::make_unique<wxui::DesktopWindow>(
			    wxui::DesktopWindowSpec{"Localization tests"},
			    R"(<Window><VerticalLayout>
                <Button name="action" textid="action" text="原文" tooltip="原提示" tooltipid="tip" height="32"/>
                <Edit name="input" text="user input" hint="原占位" hintid="hint" tooltipid="tip" height="32"/>
                <RichEdit name="help" text="原说明" textid="help" readonly="true" height="60"/>
                <TreeView name="tree"/>
            </VerticalLayout></Window>)",
			    devtools::assets::Load);
			window_->Poster()([this] { RunChecks(); });
			return true;
		}
		void RunChecks() {
			try {
				// 语言表独立测试：地区回退、默认语言、原文、空译文以及损坏包回滚。
				wxui::Localization strings;
				strings.LoadXml("en", R"(<Language><String id="action">Run &amp; inspect</String><String id="empty"></String><String id="spaces">  </String></Language>)");
				strings.LoadXml("zh-CN", R"(<Language><String id="action">执行</String><String id="only.zh">中文回退</String></Language>)");
				strings.SetFallbackLanguage("zh-CN");
				strings.SetLanguage("EN_us");
				Check(strings.GetLanguage() == "en-us", "normalize language tags");
				Check(strings.Translate("action") == "Run & inspect", "regional language fallback and XML entities");
				Check(strings.Translate("only.zh") == "中文回退", "default language fallback");
				Check(strings.Translate("missing", "original") == "original", "source text fallback");
				Check(strings.Translate("missing") == "missing", "key fallback");
				Check(strings.Translate("empty", "nonempty").empty(), "intentional empty translation");
				Check(strings.Translate("spaces") == "  ", "preserve translation whitespace");
				Reject([&] { strings.LoadXml("en", "<Language><String"); }, "reject malformed XML");
				Reject([&] { strings.LoadXml("en", "<Language><String id='a'>A</String><String id='a'>B</String></Language>"); }, "reject duplicate keys");
				Reject([&] { strings.LoadXml("en", "<Language><String id='a'><Label/></String></Language>"); }, "reject nested markup");
				Reject([&] { strings.SetLanguage("../en"); }, "reject invalid language tag");
				Check(strings.Translate("action") == "Run & inspect", "failed load preserves active translations");

				auto* action = window_->Require<wxui::Button>("action");
				auto* input = window_->Require<wxui::Edit>("input");
				auto* help = window_->Require<wxui::RichEdit>("help");
				auto* tree = window_->Require<wxui::TreeView>("tree");
				Check(action->GetText() == "原文", "fallback before language resource load, independent of XML attribute order");
				auto parent = std::make_shared<wxui::TreeNode>();
				auto child = std::make_shared<wxui::TreeNode>();
				child->BindTranslation("text", "action", "原文");
				parent->SetExpanded(false);
				parent->AddTreeChild(child);
				tree->AddRoot(parent);

				int clicks = 0;
				action->Bind("click", [&](const wxui::NotifyEvent&) { ++clicks; });
				int edits = 0;
				input->Bind("valuechanged", [&](const wxui::NotifyEvent&) { ++edits; });
				const std::string draft = "{\"text\":\"保留用户请求\"}";
				input->SetValueUtf8(draft);
				edits = 0;
				window_->LoadLanguageXml("en", "<Language><String id='action'>Run</String><String id='tip'>Tooltip</String><String id='hint'>Enter request</String><String id='help'>Help</String></Language>");
				window_->LoadLanguageXml("zh-CN", "<Language><String id='action'>执行</String><String id='tip'>提示</String><String id='hint'>请输入</String><String id='help'>说明</String></Language>");
				for (int i = 0; i < 3; ++i) {
					window_->SetLanguage("en-US");
					Check(action->GetText() == "Run" && action->GetTooltip() == "Tooltip", "live label and tooltip translation");
					Check(help->GetValueUtf8() == "Help", "live native rich edit translation");
					Check(child->GetText() == "Run", "collapsed tree node translation");
					bool nativeHint = false;
					for (auto* childWindow : input->GetManager()->GetChildren()) {
						if (auto* edit = dynamic_cast<wxTextCtrl*>(childWindow)) {
							if (edit->GetValue() == wxui::Utf8ToWxString(draft)) {
								nativeHint = edit->GetHint() == "Enter request" && edit->GetToolTipText() == "Tooltip";
							}
						}
					}
					Check(nativeHint, "native input hint and tooltip are translated");
					window_->SetLanguage("zh-CN");
					Check(action->GetText() == "执行" && help->GetValueUtf8() == "说明", "switch back to Chinese");
					Check(input->GetValueUtf8() == draft && edits == 0, "hint translation preserves typed input without emitting edits");
					Check(window_->FindControl("action") == action, "language switching must not recreate controls");
				}
				window_->RefreshLayout();
				const auto point = action->GetRect().GetTopLeft() + wxPoint(2, 2);
				wxMouseEvent motion(wxEVT_MOTION);
				motion.SetPosition(point);
				action->GetManager()->GetEventHandler()->ProcessEvent(motion);
				Check(action->GetManager()->GetToolTipText() == wxui::Utf8ToWxString("提示"), "owner-drawn hover tooltip");
				window_->SetLanguage("en");
				Check(action->GetManager()->GetToolTipText() == "Tooltip", "hover tooltip changes without moving pointer");
				action->OnButtonDown(point);
				action->OnButtonUp(point);
				Check(clicks == 1, "event bindings survive switching");
				action->UnbindTranslation("text");
				action->SetText("manual");
				window_->SetLanguage("en");
				Check(action->GetText() == "manual", "explicit unbinding preserves dynamic text");
				Reject([&] { window_->LoadLanguageResource("fr", "languages/missing.xml"); }, "missing embedded resource is reported");

				// 使用真正随包的语言资源，验证不需要额外的翻译文件。
				window_->LoadLanguageResource("en", "languages/en.xml");
				window_->LoadLanguageResource("zh-CN", "languages/zh-CN.xml");
				Check(window_->Translate("加载并检查") == "Load SDK", "embedded English resource");
				window_->SetLanguage("zh-CN");
				Check(window_->Translate("加载并检查") == "加载并检查", "embedded Chinese resource");
				{
					wxui::DesktopWindow other({"Independent window"}, "<Window><Label name='text' text='Other' textid='action'/></Window>");
					other.LoadLanguageXml("en", "<Language><String id='action'>Independent</String></Language>");
					Check(other.Require<wxui::Label>("text")->GetText() == "Independent", "window-local language tables");
					Check(window_->GetLanguage() == "zh-cn", "second window does not change first language");
				}
				std::cout << "PASS: localization fallback, resources, live controls, state preservation and isolated windows\n";
			}
			catch (const std::exception& error) {
				std::cerr << error.what() << '\n';
				++failures_;
			}
			window_->FinishClose();
		}
		int OnRun() override {
			wxui::Application::OnRun();
			return failures_ ? 1 : 0;
		}
		int OnAppExit() override {
			window_.reset();
			return 0;
		}
	};
}
WXUI_IMPLEMENT_APPLICATION(LocalizationTest);
