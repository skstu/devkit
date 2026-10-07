#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace wxui {
// UI-thread timer. Stop/destruction discards further ticks; nested modal loops
// still dispatch ticks, just like the application's main event loop.
class Timer {
public:
  Timer();
  ~Timer();
  Timer(const Timer &) = delete;
  Timer &operator=(const Timer &) = delete;
  void Start(int milliseconds, std::function<void()> callback);
  void Stop();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

using UtcTime = std::chrono::sys_seconds;
UtcTime UtcNow();
// Strict YYYY-MM-DDTHH:MM:SSZ. Parsing is independent of the local time zone.
std::optional<UtcTime> ParseUtc(std::string_view text);
std::string FormatUtc(UtcTime time);
// Accepts ISO UTC timestamps, including fractional seconds. Invalid input is
// returned unchanged so a display never substitutes an invented date.
std::string FormatLocalTime(std::string_view utc,
                            std::string_view format = "%d/%m/%Y %H:%M:%S");
bool WriteFileAtomically(const std::string &path, std::string_view bytes);
void EnableTooltips(bool enabled);
void Bell();
bool ContainsIgnoringCase(std::string_view text, std::string_view query);
} // namespace wxui
