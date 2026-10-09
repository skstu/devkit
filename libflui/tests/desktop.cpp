#include <iostream>
#include <libflui/flui.hpp>
int main() {
  try {
    auto list = std::make_shared<flui::List>();
    auto header = std::make_shared<flui::ListHeader>();
    list->Add(header);
    list->SetHeader(header.get());
    bool clicked = false;
    list->SetVirtualItems(10000, 24, [&](int) {
      auto row = std::make_shared<flui::ListContainerElement>();
      auto b = std::make_shared<flui::Button>();
      b->Bind("click", [&](const auto &) { clicked = true; });
      row->Add(b);
      return row;
    });
    if (list->GetChild(0) != header.get() || list->ChildCount() > 129 ||
        list->GetItemCount() != 10000)
      throw std::runtime_error("virtual materialization lost its header");
    auto window = std::make_shared<flui::WindowState>();
    window->root = list;
    window->Attach(list);
    auto *button = list->GetChild(1)->GetChild(0);
    list->SetEnabled(false);
    window->Event(FLUI_EVENT_ACTION, FLUI_OK,
                  std::to_string(button->id) + ":click", "");
    if (clicked)
      throw std::runtime_error(
          "disabled virtual ancestor accepted stale action");
    list->SetEnabled(true);
    window->Event(FLUI_EVENT_ACTION, FLUI_OK,
                  std::to_string(button->id) + ":click", "");
    if (!clicked)
      throw std::runtime_error("enabled virtual action lost");
    auto retired = list->GetChild(1)->shared_from_this();
    auto id = button->id;
    list->Receive("viewport", "200,10,4800");
    clicked = false;
    window->Event(FLUI_EVENT_ACTION, FLUI_OK, std::to_string(id) + ":click",
                  "");
    if (clicked || list->GetChild(0) != header.get())
      throw std::runtime_error("retired virtual action or header lifetime");
    list->RemoveAll();
    if (list->GetHeader() || list->GetItemCount())
      throw std::runtime_error("removed list retains header or virtual count");
    list->ClearItems();
    auto edit = std::make_shared<flui::Edit>();
    window->root = edit;
    window->Attach(edit);
    edit->SetValueUtf8("old");
    const auto revision = edit->Attribute("edit_revision");
    window->dirty.clear();
    window->Event(FLUI_EVENT_ACTION, FLUI_OK,
                  std::to_string(edit->id) + ":valuechanged", "new draft");
    if (!window->dirty.contains(edit->id) ||
        edit->GetValueUtf8() != "new draft" ||
        edit->Attribute("edit_revision") != revision)
      throw std::runtime_error("user edit lost retained echo or forced the caret");
    std::cout << "PASS retained virtual header, disabled ancestors, retired "
                 "actions and user edit echo without forced revision\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
