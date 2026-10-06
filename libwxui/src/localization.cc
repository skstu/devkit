#include <libwxui/localization.hpp>
#include <libwxui/types.hpp>
#include <wx/log.h>
#include <wx/sstream.h>
#include <wx/xml/xml.h>
#include <stdexcept>

namespace wxui {
	namespace {
		std::string NormalizeLanguage(std::string language) {
			if (language.empty())
				throw std::invalid_argument("Language must not be empty");
			// en_US / en-US 等价；只处理语言标记，不改变 UTF-8 文案。
			for (auto& c : language) {
				if (c == '_')
					c = '-';
				if (c >= 'A' && c <= 'Z')
					c += 'a' - 'A';
				if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
					throw std::invalid_argument("Invalid language tag: " + language);
			}
			if (language.front() == '-' || language.back() == '-' || language.find("--") != std::string::npos)
				throw std::invalid_argument("Invalid language tag: " + language);
			return language;
		}
	}

	void Localization::LoadXml(const std::string& language, const std::string& xml) {
		const auto tag = NormalizeLanguage(language);
		wxStringInputStream stream(Utf8ToWxString(xml));
		wxXmlDocument document;
		wxLogNull quiet; // 由调用方处理异常，不弹出解析器的原生错误窗口。
#if wxCHECK_VERSION(3, 3, 0)
		const bool loaded = document.Load(stream, wxXMLDOC_KEEP_WHITESPACE_NODES);
#else
		const bool loaded = document.Load(stream, "UTF-8", wxXMLDOC_KEEP_WHITESPACE_NODES);
#endif
		if (!loaded || !document.GetRoot() || document.GetRoot()->GetName() != "Language")
			throw std::invalid_argument("Invalid language XML: " + tag);
		Dictionary entries;
		for (auto* node = document.GetRoot()->GetChildren(); node; node = node->GetNext()) {
			if (node->GetType() == wxXML_COMMENT_NODE || node->GetType() == wxXML_TEXT_NODE)
				continue;
			if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != "String")
				throw std::invalid_argument("Language XML expects String elements");
			const auto id = WxStringToUtf8(node->GetAttribute("id"));
			if (id.empty())
				throw std::invalid_argument("Language string requires an id");
			for (auto* child = node->GetChildren(); child; child = child->GetNext()) {
				if (child->GetType() != wxXML_TEXT_NODE && child->GetType() != wxXML_CDATA_SECTION_NODE)
					throw std::invalid_argument("Language string must contain text only: " + id);
			}
			if (!entries.emplace(id, WxStringToUtf8(node->GetNodeContent())).second)
				throw std::invalid_argument("Duplicate language string: " + id);
		}
		// 校验全部通过后再替换，避免损坏语言包把当前界面变成半翻译状态。
		languages_[tag] = std::move(entries);
	}

	void Localization::SetLanguage(const std::string& language) {
		language_ = NormalizeLanguage(language);
	}

	void Localization::SetFallbackLanguage(const std::string& language) {
		fallbackLanguage_ = NormalizeLanguage(language);
	}

	const std::string* Localization::Find(const std::string& language, const std::string& key) const {
		auto tag = language;
		for (;;) {
			const auto dictionary = languages_.find(tag);
			if (dictionary != languages_.end()) {
				const auto entry = dictionary->second.find(key);
				if (entry != dictionary->second.end())
					return &entry->second;
			}
			// 地区包缺项时查找基础语言，例如 en-GB -> en。
			const auto separator = tag.rfind('-');
			if (separator == std::string::npos)
				return nullptr;
			tag.resize(separator);
		}
	}

	std::string Localization::Translate(const std::string& key, const std::string& fallback) const {
		if (const auto* text = Find(language_, key))
			return *text;
		if (const auto* text = Find(fallbackLanguage_, key))
			return *text;
		return fallback.empty() ? key : fallback;
	}
} // namespace wxui
