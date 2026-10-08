#include "controller.hpp"
#include "layouts.hpp"
#include <iostream>
#include <libflui/flui.h>
#include <string_view>

namespace {
flui_string span(std::string_view s) { return {s.data(), s.size()}; }
struct View {
  quote_sample::Controller controller;
  flui_window window = 0;
  uint64_t request = 2;
  bool loaded = false;
  bool failed = false;
  unsigned ticks = 0;
  bool selfTest = false;
  void publish() {
    auto status =
        flui_window_set_state(window, span(controller.state_json()), request++);
    if (status != FLUI_OK && status != FLUI_BUSY)
      failed = true;
  }
  static void event(const flui_event *event, void *user) {
    auto &self = *static_cast<View *>(user);
    if (event->kind == FLUI_EVENT_READY) {
      if (flui_window_load_xml(self.window, span(flui_xml), 1) != FLUI_OK) {
        self.failed = true;
        flui_app_quit();
      }
    } else if (event->kind == FLUI_EVENT_COMPLETE) {
      if (event->status != FLUI_OK) {
        std::cerr << std::string_view(event->value.data, event->value.size)
                  << '\n';
        self.failed = true;
        flui_app_quit();
        return;
      }
      if (event->request_id == 1) {
        self.loaded = true;
        self.publish();
      }
    } else if (event->kind == FLUI_EVENT_ACTION) {
      self.controller.dispatch(
          std::string_view(event->name.data, event->name.size),
          std::string_view(event->value.data, event->value.size));
      self.publish();
    } else if (event->kind == FLUI_EVENT_TICK) {
      if (!self.loaded) {
        if (++self.ticks > 20) {
          self.failed = true;
          flui_app_quit();
        }
        return;
      }
      self.controller.tick();
      self.publish();
      if (self.selfTest && ++self.ticks >= 3)
        flui_app_quit();
    } else if (event->kind == FLUI_EVENT_CLOSED)
      flui_app_quit();
    else if (event->kind == FLUI_EVENT_ERROR) {
      self.failed = true;
      flui_app_quit();
    }
  }
};
} // namespace
int main(int argc, char **argv) {
  View view;
  view.selfTest = argc > 1 && std::string_view(argv[1]) == "--self-test";
  flui_window_options options{};
  options.struct_size = sizeof(options);
  options.abi_version = FLUI_ABI_VERSION;
  options.width = 660;
  options.height = 490;
  options.title = span("libflui · 报价样板");
  options.on_event = View::event;
  options.user = &view;
  if (flui_window_create(&options, &view.window) != FLUI_OK)
    return 1;
  flui_window_show(view.window);
  flui_window_set_tick(view.window, 500);
  flui_app_run();
  flui_window_destroy(view.window);
  return view.failed ? 1 : 0;
}
