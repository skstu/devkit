#include <libwxui.hpp>
#include <stdexcept>
#include <unordered_set>

namespace wxui {

	void UIManager::UpdateTooltip() {
		const auto text = hovered_ ? Utf8ToWxString(hovered_->GetTooltip()) : wxString{};
		if (GetToolTipText() != text)
			SetToolTip(text);
	}

	void Control::SetManager(UIManager* manager) {
		const bool changed = manager_ != manager;
		manager_ = manager;
		if (changed && manager_)
			ApplyTranslations(manager_->GetLocalization());
		OnManagerSet();
	}

	void Control::BindTranslation(const std::string& attribute, const std::string& key,
	                              const std::string& fallback) {
		if (attribute != "text" && attribute != "hint" && attribute != "tooltip")
			throw std::invalid_argument("Unsupported translated attribute: " + attribute);
		if (key.empty())
			throw std::invalid_argument("Translation key must not be empty");
		translations_[attribute] = {key, fallback};
		if (manager_)
			ApplyTranslations(manager_->GetLocalization());
	}

	void Control::UnbindTranslation(const std::string& attribute) {
		translations_.erase(attribute);
	}

	void Control::ApplyTranslations(const Localization& language) {
		for (const auto& [attribute, binding] : translations_) {
			// 使用控件自身的属性接口，同时更新自绘文本和原生 Edit/RichEdit。
			SetAttribute(attribute, language.Translate(binding.key, binding.fallback));
		}
	}

	void UIManager::LoadLanguageXml(const std::string& language, const std::string& xml) {
		localization_.LoadXml(language, xml);
		RefreshTranslations();
	}

	void UIManager::SetLanguage(const std::string& language) {
		localization_.SetLanguage(language);
		RefreshTranslations();
	}

	void UIManager::SetFallbackLanguage(const std::string& language) {
		localization_.SetFallbackLanguage(language);
		RefreshTranslations();
	}

	void UIManager::RefreshTranslations() {
		// 原控件、编辑内容和事件绑定保持不变，只更新显式绑定的文案。
		std::unordered_set<Control*> visited;
		std::function<void(Control*)> visit = [&](Control* control) {
			if (!control || !visited.insert(control).second)
				return;
			control->ApplyTranslations(localization_);
			if (auto* tree = dynamic_cast<TreeView*>(control)) {
				for (const auto& node : tree->GetRoots())
					visit(node.get());
			}
			if (auto* node = dynamic_cast<TreeNode*>(control)) {
				// 折叠节点也要翻译，不能只遍历当前可见的平铺列表。
				for (const auto& child : node->GetTreeChildren())
					visit(child.get());
			}
			if (auto* container = dynamic_cast<Container*>(control)) {
				for (const auto& child : container->GetChildren())
					visit(child.get());
			}
		};
		visit(root_.get());
		RequestLayout();
	}

} // namespace wxui
