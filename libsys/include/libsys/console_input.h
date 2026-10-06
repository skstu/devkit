#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace libsys {
struct ConsoleInputOptions {
  std::size_t max_line_bytes = 4096;
  // Bound work per Poll even for a continuous stream without newlines.
  std::size_t max_reads_per_poll = 256;
  // Windows interactive console only; POSIX preserves the terminal's echo mode.
  bool echo = true;
};

// Owns exclusive reading of process stdin, not stdin itself. Do not mix with
// cin/getline/other readers. Construct and Poll on one thread; only one live
// instance is allowed. Destruction closes only its duplicated OS handle.
// Poll never waits for a complete line. nullopt means idle or EOF (see ended).
// Empty lines are real values. LF/CRLF/CR delimit lines; EOF returns a final
// unterminated line once. Pipes/files preserve bytes; Windows console is UTF-8.
// OS failures throw system_error; oversized lines throw length_error. Either
// failure is terminal for this object. No worker thread, exit command, SDK state,
// signal handler or application shutdown policy is owned by this class.
class ConsoleInput final {
public:
  explicit ConsoleInput(ConsoleInputOptions options = {});
  ~ConsoleInput();
  ConsoleInput(const ConsoleInput&) = delete;
  ConsoleInput& operator=(const ConsoleInput&) = delete;
  ConsoleInput(ConsoleInput&&) = delete;
  ConsoleInput& operator=(ConsoleInput&&) = delete;
  std::optional<std::string> Poll();
  bool ended() const noexcept;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace libsys
