#include <charconv>
#include <cstdio>
#include <libwxui/desktop_util.hpp>
#include <libwxui/types.hpp>
#include <stdexcept>
#include <wx/datetime.h>
#include <wx/file.h>
#include <wx/timer.h>
#include <wx/tooltip.h>
#include <wx/utils.h>

namespace wxui {
struct Timer::Impl : wxTimer {
  std::function<void()> callback;
  void Notify() override {
    // The callback may stop/restart the timer or destroy its owner.
    auto current = callback;
    if (current)
      current();
  }
};
Timer::Timer() = default;
Timer::~Timer() { Stop(); }
void Timer::Start(int milliseconds, std::function<void()> callback) {
  if (milliseconds <= 0 || !callback)
    throw std::invalid_argument("Invalid UI timer");
  if (!impl_)
    impl_ = std::make_unique<Impl>();
  impl_->Stop();
  impl_->callback = std::move(callback);
  if (!impl_->Start(milliseconds))
    throw std::runtime_error("Cannot start UI timer");
}
void Timer::Stop() {
  if (impl_) {
    impl_->Stop();
    impl_->callback = {};
  }
}
UtcTime UtcNow() {
  return std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::now());
}
std::optional<UtcTime> ParseUtc(std::string_view text) {
  if (text.size() != 20 || text[4] != '-' || text[7] != '-' ||
      text[10] != 'T' || text[13] != ':' || text[16] != ':' || text[19] != 'Z')
    return {};
  auto number = [&](size_t offset, size_t length) {
    int value = -1;
    const auto digits = text.substr(offset, length);
    if (digits.find_first_not_of("0123456789") != digits.npos)
      return -1;
    const auto result =
        std::from_chars(digits.data(), digits.data() + digits.size(), value);
    return result.ec == std::errc{} ? value : -1;
  };
  using namespace std::chrono;
  const int y = number(0, 4), m = number(5, 2), d = number(8, 2),
            h = number(11, 2), n = number(14, 2), s = number(17, 2);
  const year_month_day date{year(y), month(unsigned(m)), day(unsigned(d))};
  if (y < 1 || !date.ok() || h < 0 || h > 23 || n < 0 || n > 59 || s < 0 ||
      s > 59)
    return {};
  return sys_days(date) + hours(h) + minutes(n) + seconds(s);
}
std::string FormatUtc(UtcTime time) {
  using namespace std::chrono;
  const auto day = floor<days>(time);
  const year_month_day date{day};
  if (int(date.year()) < 1 || int(date.year()) > 9999)
    throw std::out_of_range("UTC year");
  const hh_mm_ss clock{time - day};
  char output[21];
  std::snprintf(output, sizeof(output), "%04d-%02u-%02uT%02d:%02d:%02dZ",
                int(date.year()), unsigned(date.month()), unsigned(date.day()),
                int(clock.hours().count()), int(clock.minutes().count()),
                int(clock.seconds().count()));
  return output;
}
std::string FormatLocalTime(std::string_view text, std::string_view format) {
  std::string seconds(text);
  if (text.size() > 20) {
    if (text[19] != '.' || text.back() != 'Z' || text.size() == 21 ||
        text.substr(20, text.size() - 21).find_first_not_of("0123456789") !=
            text.npos)
      return std::string(text);
    seconds = std::string(text.substr(0, 19)) + 'Z';
  }
  const auto time = ParseUtc(seconds);
  if (!time)
    return std::string(text);
  const wxDateTime date(std::chrono::system_clock::to_time_t(*time));
  return WxStringToUtf8(date.Format(Utf8ToWxString(format)));
}
bool WriteFileAtomically(const std::string &path, std::string_view bytes) {
  wxTempFile output(Utf8ToWxString(path));
  return output.IsOpened() && output.Write(bytes.data(), bytes.size()) &&
         output.Commit();
}
void EnableTooltips(bool enabled) { wxToolTip::Enable(enabled); }
void Bell() { wxBell(); }
bool ContainsIgnoringCase(std::string_view text, std::string_view query) {
  return Utf8ToWxString(text).Lower().Find(Utf8ToWxString(query).Lower()) !=
         wxNOT_FOUND;
}
} // namespace wxui
