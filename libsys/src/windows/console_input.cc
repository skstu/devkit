#include "../console_input_internal.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <system_error>

namespace libsys::detail {
namespace {
class WindowsInput final : public ConsoleBackend {
public:
  explicit WindowsInput(bool echo) : echo_(echo) {
    const HANDLE source = GetStdHandle(STD_INPUT_HANDLE);
    if (!source || source == INVALID_HANDLE_VALUE)
      throw std::system_error(ERROR_INVALID_HANDLE, std::system_category(), "stdin unavailable");
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &input_, 0, FALSE, DUPLICATE_SAME_ACCESS))
      Fail("Duplicate stdin");
    DWORD mode = 0;
    console_ = GetConsoleMode(input_, &mode) != FALSE;
    type_ = GetFileType(input_);
    output_ = GetStdHandle(STD_OUTPUT_HANDLE);
    // Never echo console keystrokes into a redirected output stream.
    echo_ = echo_ && console_ && GetConsoleMode(output_, &mode);
  }
  ~WindowsInput() override { CloseHandle(input_); }
  InputUnit Read() override {
    if (console_) return ReadKey();
    if (offset_ == count_) {
      DWORD wanted = static_cast<DWORD>(bytes_.size());
      if (type_ == FILE_TYPE_PIPE) {
        DWORD available = 0;
        if (!PeekNamedPipe(input_, nullptr, 0, nullptr, &available, nullptr)) {
          if (GetLastError() == ERROR_BROKEN_PIPE) return {InputKind::end, {}};
          Fail("Poll stdin pipe");
        }
        if (!available) return {};
        if (wanted > available) wanted = available;
      }
      DWORD count = 0;
      if (!ReadFile(input_, bytes_.data(), wanted, &count, nullptr)) {
        const auto error = GetLastError();
        if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF) return {InputKind::end, {}};
        if (error != ERROR_MORE_DATA || count == 0) Fail("Read stdin", error);
      }
      if (!count) return {InputKind::end, {}};
      count_ = count;
      offset_ = 0;
    }
    return {InputKind::text, std::string(1, bytes_[offset_++])};
  }
  void Echo(std::string_view text) override {
    if (!echo_ || text.empty()) return;
    std::array<wchar_t, 8> wide{};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), wide.data(), static_cast<int>(wide.size()));
    DWORD count = 0;
    if (!size || !WriteConsoleW(output_, wide.data(), static_cast<DWORD>(size), &count, nullptr)) Fail("Echo console input");
  }
private:
  [[noreturn]] static void Fail(const char* message, DWORD error = GetLastError()) {
    throw std::system_error(static_cast<int>(error), std::system_category(), message);
  }
  InputUnit ReadKey() {
    if (!repeats_) {
      DWORD available = 0;
      if (!GetNumberOfConsoleInputEvents(input_, &available)) Fail("Poll console input");
      if (!available) return {};
      INPUT_RECORD event{};
      DWORD count = 0;
      if (!ReadConsoleInputW(input_, &event, 1, &count)) Fail("Read console input");
      if (!count || event.EventType != KEY_EVENT || !event.Event.KeyEvent.bKeyDown)
        return {InputKind::text, {}};
      key_ = event.Event.KeyEvent.uChar.UnicodeChar;
      repeats_ = event.Event.KeyEvent.wRepeatCount;
      if (!repeats_) return {InputKind::text, {}};
    }
    --repeats_;
    if (key_ == 26) return {InputKind::end, {}}; // Console Ctrl+Z.
    if (key_ == 8) { high_surrogate_ = 0; return {InputKind::erase, {}}; }
    if (!key_) return {InputKind::text, {}}; // Function/arrow keys need no second blocking read.
    if (key_ >= 0xd800 && key_ <= 0xdbff) {
      high_surrogate_ = key_;
      return {InputKind::text, {}};
    }
    std::array<wchar_t, 2> units{};
    int length = 1;
    if (high_surrogate_ && key_ >= 0xdc00 && key_ <= 0xdfff) {
      units = {high_surrogate_, key_}; length = 2;
    } else {
      units[0] = (key_ >= 0xdc00 && key_ <= 0xdfff) ? wchar_t{0xfffd} : key_;
    }
    high_surrogate_ = 0;
    std::array<char, 8> encoded{};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, units.data(), length,
        encoded.data(), static_cast<int>(encoded.size()), nullptr, nullptr);
    if (!size) Fail("Encode console input");
    return {InputKind::text, std::string(encoded.data(), static_cast<std::size_t>(size))};
  }
  HANDLE input_ = INVALID_HANDLE_VALUE, output_ = INVALID_HANDLE_VALUE;
  bool echo_ = false, console_ = false;
  DWORD type_ = FILE_TYPE_UNKNOWN;
  wchar_t key_ = 0, high_surrogate_ = 0;
  WORD repeats_ = 0;
  std::array<char, 256> bytes_{};
  std::size_t count_ = 0, offset_ = 0;
};
}
std::unique_ptr<ConsoleBackend> OpenConsoleInput(bool echo) { return std::make_unique<WindowsInput>(echo); }
} // namespace libsys::detail
