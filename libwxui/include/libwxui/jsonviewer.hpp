#pragma once
/// libwxui — read-only JSON viewer with formatting and syntax highlighting.

#include "container.hpp"

#include <wx/textctrl.h>

#include <string>
#include <string_view>

namespace wxui {
	class TextEditor;

	enum class JsonViewMode
	{
		Raw,
		Pretty,
		Minified,
	};

	class JsonViewer : public Container {
	public:
		~JsonViewer() override;
		void DoPaint(wxDC& dc, const wxRect& clipRect) override;
		void SetAttribute(const std::string& key, const std::string& val) override;
		std::string GetTag() const override {
			return "JsonViewer";
		}

		void SetRect(const wxRect& rc) override;
		void OnManagerSet() override;
		void SetVisible(bool v) override;
		void SetEnabled(bool e) override;

		void SetJson(std::string_view json);
		void SetMode(JsonViewMode mode);
		void SetWrapLines(bool wrap);

		[[nodiscard]] std::string GetRawJson() const {
			return rawJson_;
		}
		[[nodiscard]] std::string GetDisplayedText() const;
		[[nodiscard]] JsonViewMode GetMode() const {
			return mode_;
		}
		[[nodiscard]] bool GetWrapLines() const {
			return wrapLines_;
		}

	private:
		void CreateNativeCtrls();
		void SyncStyle();
		void InvalidateRenderedControls();
		void RenderActiveText();
		void RefreshText();
		void SetTextCtrlValue(TextEditor* ctrl, const wxString& text);
		void ApplyJsonHighlight(TextEditor* ctrl, const wxString& text);
		TextEditor* ActiveTextCtrl() const;
		bool& ActiveRenderedFlag();
		const std::string& TextForMode(JsonViewMode mode);

		TextEditor* wrappedCtrl_ = nullptr;
		TextEditor* nowrapCtrl_ = nullptr;
		std::string rawJson_;
		std::string displayedText_;
		std::string prettyJson_;
		std::string minifiedJson_;
		JsonViewMode mode_ = JsonViewMode::Pretty;
		bool wrapLines_ = true;
		bool prettyJsonValid_ = false;
		bool minifiedJsonValid_ = false;
		bool wrappedRendered_ = false;
		bool nowrapRendered_ = false;
		bool transparent_ = true;
		wxColour textColor_;
		int fontId_ = -1;
	};

} // namespace wxui
