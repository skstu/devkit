#include <libsys.hpp>

#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <pwd.h>
#include <sstream>
#include <string_view>
#include <thread>
#include "../text_encoding.hpp"

extern char **environ;

namespace {
stl::path GetInstanceLockDir() {
  stl::path root = ISystem::GetUserAppDataDir();
  if (root.empty())
    root = "/tmp";
  return root.lexically_normal();
}

bool EnsureCloseOnExec(int fd) {
  if (fd < 0)
    return false;
#if defined(FD_CLOEXEC)
  const int flags = ::fcntl(fd, F_GETFD, 0);
  if (flags < 0)
    return false;
  return ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
#else
  return true;
#endif
}

bool ParsePidName(const std::filesystem::directory_entry &entry, pid_t &pid) {
  pid = 0;
  std::error_code ec;
  if (!entry.is_directory(ec) || ec)
    return false;

  const std::string name = entry.path().filename().string();
  if (name.empty())
    return false;

  errno = 0;
  char *end = nullptr;
  const long value = std::strtol(name.c_str(), &end, 10);
  if (errno != 0 || end == name.c_str() || *end != '\0' || value <= 0 ||
      value > static_cast<long>(std::numeric_limits<pid_t>::max()))
    return false;

  pid = static_cast<pid_t>(value);
  return true;
}

std::string ReadSmallFile(const std::filesystem::path &path,
                          size_t max_bytes = 256 * 1024) {
  int flags = O_RDONLY;
#if defined(O_CLOEXEC)
  flags |= O_CLOEXEC;
#endif
  const int fd = ::open(path.c_str(), flags);
  if (fd < 0)
    return {};

  std::string data;
  char buffer[4096] = {};
  while (data.size() < max_bytes) {
    const size_t remaining = max_bytes - data.size();
    const size_t chunk_size = std::min(sizeof(buffer), remaining);
    const ssize_t n = ::read(fd, buffer, chunk_size);
    if (n > 0) {
      data.append(buffer, static_cast<size_t>(n));
      continue;
    }
    if (n < 0 && errno == EINTR)
      continue;
    break;
  }
  ::close(fd);
  return data;
}

std::string TrimLine(std::string value) {
  auto is_trim = [](char ch) {
    return ch == '\0' || ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t';
  };

  while (!value.empty() && is_trim(value.back()))
    value.pop_back();

  size_t first = 0;
  while (first < value.size() && is_trim(value[first]))
    ++first;
  if (first > 0)
    value.erase(0, first);
  return value;
}

system_process_id_t ReadParentPid(const std::filesystem::path &stat_path) {
  const std::string stat = ReadSmallFile(stat_path);
  if (stat.empty())
    return 0;

  const size_t comm_end = stat.rfind(") ");
  if (comm_end == std::string::npos)
    return 0;

  const size_t state_end = stat.find(' ', comm_end + 2);
  if (state_end == std::string::npos)
    return 0;

  const size_t ppid_begin = stat.find_first_not_of(' ', state_end);
  if (ppid_begin == std::string::npos)
    return 0;

  errno = 0;
  char *end = nullptr;
  const long ppid = std::strtol(stat.c_str() + ppid_begin, &end, 10);
  if (errno != 0 || end == stat.c_str() + ppid_begin || ppid <= 0)
    return 0;
  return static_cast<system_process_id_t>(ppid);
}

bool IsProcZombie(system_process_id_t pid) {
  const std::filesystem::path stat_path =
      std::filesystem::path("/proc") / std::to_string(pid) / "stat";
  const std::string stat = ReadSmallFile(stat_path);
  if (stat.empty())
    return false;

  // /proc/<pid>/stat has the process name in parentheses, so locate the last
  // ") " before reading the one-character state field. This avoids waitpid()
  // and therefore never steals child reaping from libuv.
  const size_t comm_end = stat.rfind(") ");
  if (comm_end == std::string::npos)
    return false;
  const size_t state_pos = stat.find_first_not_of(' ', comm_end + 2);
  return state_pos != std::string::npos && stat[state_pos] == 'Z';
}

std::string ReadLinkUtf8(const std::filesystem::path &path) {
  std::vector<char> buffer(4096);
  const ssize_t len = ::readlink(path.c_str(), buffer.data(), buffer.size() - 1);
  if (len <= 0)
    return {};
  return std::string(buffer.data(), static_cast<size_t>(len));
}

std::string ReadCmdline(const std::filesystem::path &path) {
  std::string cmdline = ReadSmallFile(path);
  for (char &ch : cmdline) {
    if (ch == '\0')
      ch = ' ';
  }
  return TrimLine(std::move(cmdline));
}

uid_t ReadUid(const std::filesystem::path &status_path) {
  const std::string status = ReadSmallFile(status_path);
  if (status.empty())
    return static_cast<uid_t>(-1);

  size_t pos = status.find("Uid:");
  while (pos != std::string::npos && pos > 0 && status[pos - 1] != '\n')
    pos = status.find("Uid:", pos + 4);
  if (pos == std::string::npos)
    return static_cast<uid_t>(-1);

  const char *cursor = status.c_str() + pos + 4;
  while (*cursor == ' ' || *cursor == '\t')
    ++cursor;

  errno = 0;
  char *end = nullptr;
  const unsigned long uid = std::strtoul(cursor, &end, 10);
  if (errno != 0 || end == cursor)
    return static_cast<uid_t>(-1);
  return static_cast<uid_t>(uid);
}

std::string UserNameFromUid(uid_t uid) {
  if (uid == static_cast<uid_t>(-1))
    return {};

  if (passwd *pwd = ::getpwuid(uid); pwd && pwd->pw_name)
    return pwd->pw_name;
  return std::to_string(static_cast<unsigned long long>(uid));
}

// Run a desktop helper without a shell. Window-manager utilities are optional
// on Linux, so failures are expected and must never affect the browser process.
bool RunDesktopCommand(const char *program,
                       const std::vector<std::string> &arguments,
                       std::string *stdout_out, int timeout_ms) {
  if (!program || !*program || timeout_ms <= 0)
    return false;

  int output_pipe[2] = {-1, -1};
  if (::pipe(output_pipe) != 0)
    return false;
  EnsureCloseOnExec(output_pipe[0]);
  EnsureCloseOnExec(output_pipe[1]);

  std::vector<std::string> argv_storage;
  argv_storage.reserve(arguments.size() + 1);
  argv_storage.emplace_back(program);
  argv_storage.insert(argv_storage.end(), arguments.begin(), arguments.end());
  std::vector<char *> argv;
  argv.reserve(argv_storage.size() + 1);
  for (auto &argument : argv_storage)
    argv.push_back(argument.data());
  argv.push_back(nullptr);

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    ::close(output_pipe[0]);
    ::close(output_pipe[1]);
    return false;
  }
  const bool actions_ready =
      posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO) ==
          0 &&
      posix_spawn_file_actions_addclose(&actions, output_pipe[0]) == 0 &&
      (output_pipe[1] == STDOUT_FILENO ||
       posix_spawn_file_actions_addclose(&actions, output_pipe[1]) == 0);
  if (!actions_ready) {
    posix_spawn_file_actions_destroy(&actions);
    ::close(output_pipe[0]);
    ::close(output_pipe[1]);
    return false;
  }

  pid_t child_pid = 0;
  const int spawn_result = posix_spawnp(
      &child_pid, program, &actions, nullptr,
      const_cast<char *const *>(argv.data()), environ);
  posix_spawn_file_actions_destroy(&actions);
  ::close(output_pipe[1]);
  if (spawn_result != 0) {
    ::close(output_pipe[0]);
    return false;
  }

  const int current_flags = ::fcntl(output_pipe[0], F_GETFL, 0);
  if (current_flags >= 0)
    ::fcntl(output_pipe[0], F_SETFL, current_flags | O_NONBLOCK);

  std::string output;
  static constexpr size_t kMaxOutputBytes = 128 * 1024;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  int wait_status = 0;
  bool reaped = false;
  bool timed_out = false;
  bool output_closed = false;

  while (!reaped || !output_closed) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining <= std::chrono::milliseconds::zero()) {
      timed_out = true;
      break;
    }

    struct pollfd descriptor = {output_pipe[0], POLLIN | POLLHUP | POLLERR,
                                0};
    const int poll_timeout = static_cast<int>(
        std::min<int64_t>(remaining.count(), static_cast<int64_t>(50)));
    const int poll_result = ::poll(&descriptor, 1, poll_timeout);
    if (poll_result > 0 &&
        (descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      char buffer[4096];
      for (;;) {
        const ssize_t read_count = ::read(output_pipe[0], buffer, sizeof(buffer));
        if (read_count > 0) {
          if (output.size() < kMaxOutputBytes) {
            const size_t copy_count = std::min<size_t>(
                static_cast<size_t>(read_count), kMaxOutputBytes - output.size());
            output.append(buffer, copy_count);
          }
          continue;
        }
        if (read_count == 0) {
          output_closed = true;
          break;
        }
        if (errno == EINTR)
          continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break;
        output_closed = true;
        break;
      }
    }

    const pid_t wait_result = ::waitpid(child_pid, &wait_status, WNOHANG);
    if (wait_result == child_pid)
      reaped = true;
    else if (wait_result < 0 && errno != EINTR)
      reaped = true;
  }

  if (timed_out && !reaped) {
    ::kill(child_pid, SIGTERM);
    for (int attempt = 0; attempt < 4; ++attempt) {
      const pid_t wait_result = ::waitpid(child_pid, &wait_status, WNOHANG);
      if (wait_result == child_pid ||
          (wait_result < 0 && errno != EINTR)) {
        reaped = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!reaped) {
      ::kill(child_pid, SIGKILL);
      while (::waitpid(child_pid, &wait_status, 0) < 0 && errno == EINTR) {
      }
      reaped = true;
    }
  }

  ::close(output_pipe[0]);
  if (stdout_out)
    *stdout_out = std::move(output);
  return reaped && !timed_out && WIFEXITED(wait_status) &&
         WEXITSTATUS(wait_status) == 0;
}

bool ActivateWithXdotool(system_process_id_t pid) {
  const char *display = std::getenv("DISPLAY");
  if (!display || !*display)
    return false;

  std::string output;
  if (!RunDesktopCommand(
          "xdotool", {"search", "--pid", std::to_string(pid)}, &output,
          250)) {
    return false;
  }

  std::istringstream lines(output);
  std::string window_id;
  bool requested = false;
  static constexpr size_t kMaxWindowActivations = 4;
  size_t activation_count = 0;
  while (activation_count < kMaxWindowActivations && lines >> window_id) {
    ++activation_count;
    std::string ignored;
    RunDesktopCommand("xdotool", {"windowmap", window_id}, &ignored, 150);
    if (RunDesktopCommand("xdotool", {"windowactivate", "--sync", window_id},
                          &ignored, 250)) {
      RunDesktopCommand("xdotool", {"windowraise", window_id}, &ignored, 150);
      requested = true;
    }
  }
  return requested;
}

bool ActivateWithWmctrl(system_process_id_t pid) {
  const char *display = std::getenv("DISPLAY");
  if (!display || !*display)
    return false;

  std::string output;
  if (!RunDesktopCommand("wmctrl", {"-lp"}, &output, 250))
    return false;

  std::istringstream lines(output);
  std::string line;
  bool requested = false;
  static constexpr size_t kMaxWindowActivations = 4;
  size_t activation_count = 0;
  while (activation_count < kMaxWindowActivations &&
         std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string window_id;
    std::string desktop;
    std::string window_pid;
    if (!(fields >> window_id >> desktop >> window_pid) ||
        window_pid != std::to_string(pid)) {
      continue;
    }
    ++activation_count;
    std::string ignored;
    if (RunDesktopCommand("wmctrl", {"-ia", window_id}, &ignored, 250))
      requested = true;
  }
  return requested;
}
} // namespace



stl::path ISystem::GetCurrentProcessPath() {
  stl::path result;
  char path[4096] = {0};
  const ssize_t len = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (len > 0)
    result = std::string(path, static_cast<size_t>(len));
  return result;
}

stl::path ISystem::GetDllPath(void *static_dummy_variable) {
  stl::path result;
  Dl_info info = {};
  if (dladdr(static_dummy_variable, &info) != 0 && info.dli_fname)
    result = info.dli_fname;
  return result;
}

bool ISystem::LaunchProcess(const stl::path &proc,
                            const std::vector<std::string> &args,
                            system_process_id_t &pid) {
  pid = 0;
  std::vector<const char *> argv;
  const std::string proc_str = proc.string();
  argv.push_back(proc_str.c_str());
  for (const auto &a : args)
    argv.push_back(a.c_str());
  argv.push_back(nullptr);

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

  pid_t child_pid = 0;
  const int rc = posix_spawn(&child_pid, proc_str.c_str(), nullptr, &attr,
                             const_cast<char *const *>(argv.data()), environ);
  posix_spawnattr_destroy(&attr);
  if (rc != 0)
    return false;
  pid = static_cast<system_process_id_t>(child_pid);
  return true;
}

bool ISystem::LaunchProcess(const stl::path &proc,
                            const std::vector<std::u16string> &args,
                            system_process_id_t &pid) {
  std::vector<std::string> utf8_args;
  utf8_args.reserve(args.size());
  for (const auto &a : args)
    utf8_args.push_back(IConv::UTF16ToUTF8(a));
  return ISystem::LaunchProcess(proc, utf8_args, pid);
}

bool ISystem::KillProc(system_process_id_t pid, int /*signal*/) {
  if (pid <= 1)
    return false;
  ::killpg(static_cast<pid_t>(pid), SIGKILL);
  if (::kill(static_cast<pid_t>(pid), SIGKILL) == 0)
    return true;
  return errno == ESRCH;
}

bool ISystem::HasProc(system_process_id_t pid) {
  if (pid <= 0)
    return false;
  // Do not call waitpid here. Browser processes are owned and reaped by the
  // libuv runtime; probing them with waitpid(WNOHANG) can consume the exit
  // notification before uv's child watcher sees it and destabilize shutdown.
  if (IsProcZombie(pid))
    return false;
  const pid_t native_pid = static_cast<pid_t>(pid);
  if (::kill(native_pid, 0) == 0)
    return true;
  return errno == EPERM;
}

std::vector<ISystem::ProcessInfo> ISystem::ListProcesses() {
  std::vector<ProcessInfo> processes;
  std::error_code ec;
  const uid_t current_uid = ::getuid();

  for (const auto &entry : std::filesystem::directory_iterator("/proc", ec)) {
    if (ec)
      break;
    pid_t pid = 0;
    if (!ParsePidName(entry, pid))
      continue;

    const std::filesystem::path proc_dir = entry.path();
    ProcessInfo info;
    info.pid = static_cast<system_process_id_t>(pid);
    info.parent_pid = ReadParentPid(proc_dir / "stat");
    info.name = TrimLine(ReadSmallFile(proc_dir / "comm"));
    info.executable_path = ReadLinkUtf8(proc_dir / "exe");
    info.command_line = ReadCmdline(proc_dir / "cmdline");

    const uid_t uid = ReadUid(proc_dir / "status");
    info.user = UserNameFromUid(uid);
    info.current_user = uid != static_cast<uid_t>(-1) && uid == current_uid;
    info.accessible = !info.executable_path.empty() ||
                      !info.command_line.empty() || !info.user.empty();
    processes.push_back(std::move(info));
  }

  std::sort(processes.begin(), processes.end(),
            [](const ProcessInfo &lhs, const ProcessInfo &rhs) {
              return lhs.pid < rhs.pid;
            });
  return processes;
}

system_process_handle_t ISystem::acquire_instance_lock(const std::string &name,
                                                       bool &exists,const std::function<void(std::string&)>& onWriteData) {
  exists = false;
  int fd = -1;
  do {
    if (name.empty())
      break;
    const stl::path lock_dir = GetInstanceLockDir();
    std::error_code ec;
    std::filesystem::create_directories(lock_dir, ec);
    if (ec)
      break;
    const stl::path lock_path = lock_dir / (name + ".lock");
    int open_flags = O_CREAT | O_RDWR;
#if defined(O_CLOEXEC)
    open_flags |= O_CLOEXEC;
#endif
    const std::string lock_path_native = lock_path.string();
    fd = ::open(lock_path_native.c_str(), open_flags, 0600);
    if (fd < 0)
      break;
    EnsureCloseOnExec(fd);
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
      exists = (errno == EWOULDBLOCK || errno == EAGAIN);
      ::close(fd);
      fd = -1;
      break;
    }
    ::ftruncate(fd, 0);
    std::string pid_str = std::to_string(::getpid());
    if (onWriteData) {
      onWriteData(pid_str);
    }

    ::write(fd, pid_str.c_str(), pid_str.size());
  } while (0);
  return fd;
}

void ISystem::release_instance_lock(const system_process_handle_t &ph) {
  if (ph >= 0) {
    ::flock(ph, LOCK_UN);
    ::close(ph);
  }
}

bool ISystem::ActivateProcess(system_process_id_t pid) {
  if (pid <= 1 || !HasProc(pid))
    return false;

  // X11 window activation is optional and unavailable on pure Wayland. The
  // helpers are best-effort; neither path sends a signal to the browser PID.
  if (ActivateWithWmctrl(pid))
    return true;
  return ActivateWithXdotool(pid);
}

// ---------------------------------------------------------------------------
// LaunchRemoteDebuggingPipeChrome – Linux implementation
// Same approach as macOS: pipe() + posix_spawn with file actions.
// ---------------------------------------------------------------------------
bool ISystem::LaunchRemoteDebuggingPipeChrome(
  const stl::path &chrome, const std::vector<std::u16string> &extra_args,
    system_process_id_t &pid, ISystem::ChromeDevPipe &pipe_out) {
  pid = 0;
  int cmd_pipe[2] = {-1, -1};
  int resp_pipe[2] = {-1, -1};
  bool result = false;

  do {
    if (::pipe(cmd_pipe) < 0)
      break;
    if (::pipe(resp_pipe) < 0)
      break;

    const std::string proc_str = chrome.string();
    std::vector<std::string> all_args;
    //all_args.push_back("--remote-debugging-pipe");
    for (const auto &a : extra_args)
      all_args.push_back(IConv::UTF16ToUTF8(a));

    std::vector<const char *> argv;
    argv.push_back(proc_str.c_str());
    for (const auto &a : all_args)
      argv.push_back(a.c_str());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, cmd_pipe[0], 3);
    posix_spawn_file_actions_adddup2(&fa, resp_pipe[1], 4);
    if (cmd_pipe[1] != 3 && cmd_pipe[1] != 4)
      posix_spawn_file_actions_addclose(&fa, cmd_pipe[1]);
    if (resp_pipe[0] != 3 && resp_pipe[0] != 4)
      posix_spawn_file_actions_addclose(&fa, resp_pipe[0]);
    if (cmd_pipe[0] != 3 && cmd_pipe[0] != 4)
      posix_spawn_file_actions_addclose(&fa, cmd_pipe[0]);
    if (resp_pipe[1] != 3 && resp_pipe[1] != 4)
      posix_spawn_file_actions_addclose(&fa, resp_pipe[1]);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

    pid_t child_pid = 0;
    int rc = posix_spawn(&child_pid, proc_str.c_str(), &fa, &attr,
                         const_cast<char *const *>(argv.data()), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);

    if (rc != 0)
      break;

    ::close(cmd_pipe[0]);
    cmd_pipe[0] = -1;
    ::close(resp_pipe[1]);
    resp_pipe[1] = -1;

    pipe_out.write_cmd_fd = cmd_pipe[1];
    cmd_pipe[1] = -1;
    pipe_out.read_resp_fd = resp_pipe[0];
    resp_pipe[0] = -1;
    pid = static_cast<system_process_id_t>(child_pid);
    result = true;
  } while (0);

  for (int fd : {cmd_pipe[0], cmd_pipe[1], resp_pipe[0], resp_pipe[1]}) {
    if (fd >= 0)
      ::close(fd);
  }
  return result;
}
