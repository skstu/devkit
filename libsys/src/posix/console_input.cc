#include "../console_input_internal.h"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace libsys::detail {
namespace {
class PosixInput final : public ConsoleBackend {
public:
  PosixInput() {
    fd_ = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);
    if (fd_ < 0) Fail("Duplicate stdin");
    try {
      const auto flags = fcntl(fd_, F_GETFL);
      if (flags < 0) Fail("Read stdin flags");
      if (!(flags & O_NONBLOCK)) {
        if (fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) Fail("Set nonblocking stdin");
        restore_blocking_ = true;
      }
    } catch (...) { close(fd_); throw; }
  }
  ~PosixInput() override {
    if (restore_blocking_) {
      const auto flags = fcntl(fd_, F_GETFL);
      if (flags >= 0) (void)fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);
    }
    close(fd_);
  }
  InputUnit Read() override {
    if (offset_ == count_) {
      const auto count = read(fd_, bytes_.data(), bytes_.size());
      if (count < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return {};
        Fail("Read stdin");
      }
      if (!count) return {InputKind::end, {}};
      count_ = static_cast<std::size_t>(count);
      offset_ = 0;
    }
    return {InputKind::text, std::string(1, bytes_[offset_++])};
  }
private:
  [[noreturn]] static void Fail(const char* message) { throw std::system_error(errno, std::generic_category(), message); }
  int fd_ = -1;
  bool restore_blocking_ = false;
  std::array<char, 256> bytes_{};
  std::size_t count_ = 0, offset_ = 0;
};
}
std::unique_ptr<ConsoleBackend> OpenConsoleInput(bool) { return std::make_unique<PosixInput>(); }
} // namespace libsys::detail
