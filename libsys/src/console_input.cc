#include <libsys/console_input.h>
#include "console_input_internal.h"

#include <atomic>
#include <stdexcept>
#include <thread>
#include <utility>

namespace libsys {
namespace {
std::atomic_flag stdin_owned = ATOMIC_FLAG_INIT;
}
struct ConsoleInput::Impl {
  ConsoleInputOptions options;
  std::unique_ptr<detail::ConsoleBackend> backend;
  const std::thread::id owner = std::this_thread::get_id();
  std::string line;
  bool ended = false, failed = false, skip_lf = false;
  explicit Impl(ConsoleInputOptions value) : options(value) {
    if (!options.max_line_bytes || options.max_line_bytes > 1024 * 1024 ||
        !options.max_reads_per_poll || options.max_reads_per_poll > 65536)
      throw std::invalid_argument("Invalid console input limits");
    if (stdin_owned.test_and_set()) throw std::logic_error("stdin already has a ConsoleInput owner");
    try { backend = detail::OpenConsoleInput(options.echo); }
    catch (...) { stdin_owned.clear(); throw; }
  }
  ~Impl() { backend.reset(); stdin_owned.clear(); }
  std::string TakeLine() { return std::exchange(line, {}); }
  std::optional<std::string> Poll() {
    for (std::size_t i = 0; i < options.max_reads_per_poll; ++i) {
      auto input = backend->Read();
      switch (input.kind) {
      case detail::InputKind::idle: return {};
      case detail::InputKind::end:
        ended = true;
        return line.empty() ? std::nullopt : std::optional(TakeLine());
      case detail::InputKind::erase:
        if (!line.empty()) {
          // Erase one UTF-8 code point, never leave a partial sequence.
          auto position = line.size() - 1;
          while (position && (static_cast<unsigned char>(line[position]) & 0xc0) == 0x80) --position;
          line.resize(position);
          backend->Echo("\b \b");
        }
        break;
      case detail::InputKind::text:
        if (skip_lf && input.text == "\n") { skip_lf = false; break; }
        skip_lf = false;
        if (input.text == "\r" || input.text == "\n") {
          skip_lf = input.text == "\r";
          backend->Echo("\r\n");
          return TakeLine();
        }
        if (input.text.size() > options.max_line_bytes - line.size())
          throw std::length_error("Console input line exceeds max_line_bytes");
        line += input.text;
        backend->Echo(input.text);
        break;
      }
    }
    return {};
  }
};
ConsoleInput::ConsoleInput(ConsoleInputOptions options) : impl_(std::make_unique<Impl>(options)) {}
ConsoleInput::~ConsoleInput() = default;
bool ConsoleInput::ended() const noexcept { return impl_->ended; }
std::optional<std::string> ConsoleInput::Poll() {
  if (impl_->owner != std::this_thread::get_id()) throw std::logic_error("ConsoleInput requires its owning thread");
  if (impl_->failed) throw std::logic_error("ConsoleInput failed; construct a new lifecycle");
  if (impl_->ended) return {};
  try { return impl_->Poll(); }
  catch (...) { impl_->failed = true; throw; }
}
} // namespace libsys
