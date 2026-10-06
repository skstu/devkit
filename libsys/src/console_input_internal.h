#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace libsys::detail {
enum class InputKind { idle, text, erase, end };
struct InputUnit {
  InputKind kind = InputKind::idle;
  std::string text;
};
class ConsoleBackend {
public:
  virtual ~ConsoleBackend() = default;
  virtual InputUnit Read() = 0;
  virtual void Echo(std::string_view) {};
};
std::unique_ptr<ConsoleBackend> OpenConsoleInput(bool echo);
} // namespace libsys::detail
