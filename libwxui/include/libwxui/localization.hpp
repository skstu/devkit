#pragma once
#include <map>
#include <string>

namespace wxui {

	// 每个窗口独立持有语言表；加载、切换和查询都在 UI 线程执行。
	class Localization {
	public:
		// UTF-8 XML: <Language><String id="button.ok">OK</String></Language>
		// 同一语言重新加载时整体替换；解析失败抛出异常，旧语言表保持有效。
		void LoadXml(const std::string& language, const std::string& xml);
		void SetLanguage(const std::string& language);
		void SetFallbackLanguage(const std::string& language);
		const std::string& GetLanguage() const {
			return language_;
		}
		std::string Translate(const std::string& key, const std::string& fallback = {}) const;

	private:
		using Dictionary = std::map<std::string, std::string>;
		const std::string* Find(const std::string& language, const std::string& key) const;
		std::map<std::string, Dictionary> languages_;
		std::string language_ = "en";
		std::string fallbackLanguage_ = "en";
	};

} // namespace wxui
