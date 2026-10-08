#include "controller.hpp"
#include "layouts.hpp"
#include <iostream>
#include <libwxui.hpp>
namespace {
class QuotesApp final : public wxui::Application {
  quote_sample::Controller model_;
  std::unique_ptr<wxui::DesktopWindow> window_;
  std::array<wxui::ListTextElement *, 3> rows_;
  wxui::Timer timer_;
  bool failed_ = false;
  void render() {
    for (size_t i = 0; i < rows_.size(); ++i) {
      const auto &q = model_.quotes[i];
      rows_[i]->SetColumnText(0, std::string_view(q.symbol));
      rows_[i]->SetColumnText(1, std::string_view(q.bid));
      rows_[i]->SetColumnText(2, std::string_view(q.ask));
    }
    window_->Require<wxui::Label>("selection")
        ->SetText("当前：" + model_.selection().symbol);
    window_->Require<wxui::Label>("result")->SetText(model_.result);
    auto *edit = window_->Require<wxui::Edit>("quantity");
    if (edit->GetValueUtf8() != model_.quantity)
      edit->SetValueUtf8(model_.quantity);
  }
  bool OnAppInit() override {
    window_ = std::make_unique<wxui::DesktopWindow>(
        wxui::DesktopWindowSpec{
            "libwxui · 报价样板", {660, 490}, {600, 460}, 0},
        wxui_xml);
    auto *list = window_->Require<wxui::List>("quotes");
    for (auto &row : rows_)
      row = list->AppendTextItemUtf8({"", "", ""}, 44);
    list->SelectItem(0, false);
    list->Bind("itemselect", [this](const wxui::NotifyEvent &e) {
      if (e.param1 >= 0 && e.param1 < 3)
        model_.dispatch("quote.select", model_.quotes[e.param1].id);
      render();
    });
    window_->Require<wxui::Edit>("quantity")
        ->Bind("valuechanged", [this](const auto &) {
          model_.dispatch(
              "quantity.changed",
              window_->Require<wxui::Edit>("quantity")->GetValueUtf8());
        });
    window_->Require<wxui::Button>("submit")->Bind("click", [this](
                                                                const auto &) {
      model_.dispatch("quantity.changed",
                      window_->Require<wxui::Edit>("quantity")->GetValueUtf8());
      model_.dispatch("order.submit", "");
      render();
    });
    window_->OnClose([this](bool) {
      timer_.Stop();
      return true;
    });
    window_->Present();
    render();
    timer_.Start(500, [this] {
      model_.tick();
      render();
    });
    for (const auto &arg : Arguments())
      if (arg == "--self-test")
        window_->Poster()([this] { test(); });
    return true;
  }
  void test() {
    auto require = [](bool b) {
      if (!b)
        throw std::runtime_error("wxui shared-controller roundtrip");
    };
    try {
      window_->Require<wxui::List>("quotes")->SelectItem(2);
      auto *edit = window_->Require<wxui::Edit>("quantity");
      edit->SetValueUtf8("2");
      auto *button = window_->Require<wxui::Button>("submit");
      auto rect = button->GetRect();
      button->OnButtonUp({rect.x + 5, rect.y + 5});
      require(model_.selected == "gold" && model_.quantity == "2" &&
              model_.submissions == 1);
      require(window_->Require<wxui::Label>("result")->GetText().find(
                  "GOLD × 2") != std::string::npos);
      for (int i = 0; i < 20; ++i) {
        model_.tick();
        render();
      }
      require(edit->GetValueUtf8() == "2");
      edit->SetValueUtf8("0");
      button->OnButtonUp({rect.x + 5, rect.y + 5});
      require(model_.submissions == 1 &&
              window_->Require<wxui::Label>("result")->GetText().find("1–10") !=
                  std::string::npos);
      std::cout << "PASS libwxui: list selection, quantity, click, controller "
                   "result, live quote updates\n";
    } catch (const std::exception &e) {
      failed_ = true;
      std::cerr << e.what() << '\n';
    }
    timer_.Stop();
    window_->FinishClose();
  }
  int OnRun() override {
    wxui::Application::OnRun();
    return failed_ ? 1 : 0;
  }
  int OnAppExit() override {
    timer_.Stop();
    window_.reset();
    return 0;
  }
};
} // namespace
WXUI_IMPLEMENT_APPLICATION(QuotesApp);
