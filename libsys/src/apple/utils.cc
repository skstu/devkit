#include <libsys.hpp>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <mach-o/dyld.h> // _NSGetExecutablePath
#include <dlfcn.h>       // dladdr
#include <signal.h>      // kill, SIGKILL
#include <sys/types.h>
#include <sys/file.h> // flock
#include <unistd.h>   // getpid, readlink
#include <sys/wait.h> // waitpid
#include <spawn.h>    // posix_spawn, posix_spawnattr_t
#include <pwd.h>      // getpwuid
#include "../text_encoding.hpp"

extern char **environ;

namespace {
stl::path GetInstanceLockDir() {
  //stl::path root = ISystem::GetUserAppDataDir();
  stl::path root = std::getenv("HOME");
  root = (stl::path(root) / "BroSDK").lexically_normal();
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

std::string ProcessPath(pid_t pid) {
  if (pid <= 0)
    return {};

  char path[PROC_PIDPATHINFO_MAXSIZE] = {};
  const int len = ::proc_pidpath(pid, path, sizeof(path));
  if (len <= 0)
    return {};
  return std::string(path, static_cast<size_t>(len));
}

std::string UserNameFromUid(uid_t uid) {
  if (uid == static_cast<uid_t>(-1))
    return {};

  if (passwd *pwd = ::getpwuid(uid); pwd && pwd->pw_name)
    return pwd->pw_name;
  return std::to_string(static_cast<unsigned long long>(uid));
}
} // namespace


// ---------------------------------------------------------------------------
// GetCurrentProcessPath
// ---------------------------------------------------------------------------
stl::path ISystem::GetCurrentProcessPath() {
  stl::path result;
  char buf[4096] = {};
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0) {
    result = buf;
  } else {
    // Buffer was too small; allocate and retry.
    std::vector<char> big(size);
    if (_NSGetExecutablePath(big.data(), &size) == 0)
      result = big.data();
  }
  return result;
}

// ---------------------------------------------------------------------------
// GetDllPath — resolve the path of the shared library containing the given
// symbol (pass a pointer to a static variable inside the library).
// ---------------------------------------------------------------------------
stl::path ISystem::GetDllPath(void *static_dummy_variable) {
  stl::path result;
  Dl_info info = {};
  if (dladdr(static_dummy_variable, &info) != 0 && info.dli_fname)
    result = info.dli_fname;
  return result;
}

// ---------------------------------------------------------------------------
// LaunchProcess — std::string args overload
// ---------------------------------------------------------------------------
bool ISystem::LaunchProcess(const stl::path &proc,
                            const std::vector<std::string> &args,
                            system_process_id_t &pid) {
  pid = 0;
  // Build a null-terminated argv array: argv[0] = executable path.
  std::vector<const char *> argv;
  const std::string proc_str = proc.string();
  argv.push_back(proc_str.c_str());
  for (const auto &a : args)
    argv.push_back(a.c_str());
  argv.push_back(nullptr);
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  // POSIX_SPAWN_SETPGROUP: put the child in its own process group so it
  // is not killed when the parent's terminal session ends.
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

  pid_t child_pid = 0;
  int rc = posix_spawn(&child_pid, proc_str.c_str(),
                       nullptr, // file_actions
                       &attr, const_cast<char *const *>(argv.data()), environ);
  posix_spawnattr_destroy(&attr);

  if (rc != 0) {
    return false;
  }
  pid = static_cast<system_process_id_t>(child_pid);
  return true;
}

// ---------------------------------------------------------------------------
// LaunchProcess — std::u16string args overload (convert to UTF-8 first)
// ---------------------------------------------------------------------------
bool ISystem::LaunchProcess(const stl::path &proc,
                            const std::vector<std::u16string> &args,
                            system_process_id_t &pid) {
  std::vector<std::string> utf8_args;
  utf8_args.reserve(args.size());
  for (const auto &a : args)
    utf8_args.push_back(IConv::UTF16ToUTF8(a));
  return ISystem::LaunchProcess(proc, utf8_args, pid);
}

// ---------------------------------------------------------------------------
// KillProc — terminate a process by PID (signal is ignored; always SIGKILL)
//
// On macOS, Chromium spawns GPU / renderer / network-service helpers inside
// the same process group (pgid == main browser PID, set via
// POSIX_SPAWN_SETPGROUP in LaunchProcess).  Sending SIGKILL only to the root
// PID left those helpers alive, keeping the browser window visible.  We now
// send SIGKILL to the entire group first, then to the root PID directly (in
// case the root PID is no longer the pgid leader at kill time).
//
// ESRCH ("no such process") is treated as success: the process is already
// gone, which is the desired end-state.  This also prevents the double-close
// race where CloseBrowserAsync and the Process() loop both call Close() in
// quick succession — the second kill() sees ESRCH and must not report failure.
// ---------------------------------------------------------------------------
bool ISystem::KillProc(system_process_id_t pid, int /*signal*/) {
  if (pid <= 1)
    return false;
  // Kill the whole process group (GPU helper, renderer, network service, …).
  // Ignore the return value: the group may have already disbanded.
  ::killpg(static_cast<pid_t>(pid), SIGKILL);
  // Also target the root PID directly to handle the case where it has moved
  // to a different process group.
  if (::kill(static_cast<pid_t>(pid), SIGKILL) == 0)
    return true;
  // ESRCH means the process (and its group) are already gone — that is
  // exactly the "closed" state we want.
  return errno == ESRCH;
}

// ---------------------------------------------------------------------------
// HasProc — check whether a process is still running
//
// kill(pid, 0) is NOT sufficient: it returns 0 for zombie processes because
// zombies still occupy a slot in the process table even though they have
// already exited.  This caused flash-crashing Chromium (killed by startup-
// protection checks) to be reported as alive indefinitely.
//
// We now use waitpid(WNOHANG) first:
//   r == pid : child has exited → zombie reaped, process is gone → false
//   r ==   0 : child is still running                              → true
//   r ==  -1 : ECHILD means this pid is not our child (or was already
//              reaped by a previous call); fall back to kill(0) check.
// ---------------------------------------------------------------------------
bool ISystem::HasProc(system_process_id_t pid) {
  if (pid <= 0)
    return false;
  int wstatus = 0;
  pid_t r = ::waitpid(static_cast<pid_t>(pid), &wstatus, WNOHANG);
  if (r == static_cast<pid_t>(pid))
    return false; // child exited (zombie reaped)
  if (r == 0)
    return true;  // child is still running
  // r == -1 / ECHILD: not our child or already reaped — fall back to
  // kill(0) existence check (covers processes we didn't posix_spawn).
  return ::kill(static_cast<pid_t>(pid), 0) == 0;
}

std::vector<ISystem::ProcessInfo> ISystem::ListProcesses() {
  std::vector<ProcessInfo> processes;
  const int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
  if (bytes <= 0)
    return processes;

  std::vector<pid_t> pids(static_cast<size_t>(bytes) / sizeof(pid_t));
  const int written =
      proc_listpids(PROC_ALL_PIDS, 0, pids.data(),
                    static_cast<int>(pids.size() * sizeof(pid_t)));
  if (written <= 0)
    return processes;

  pids.resize(static_cast<size_t>(written) / sizeof(pid_t));
  const uid_t current_uid = ::getuid();
  for (pid_t pid : pids) {
    if (pid <= 0)
      continue;

    proc_bsdinfo info = {};
    const int rc =
        proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info));
    if (rc <= 0)
      continue;

    ProcessInfo process;
    process.pid = static_cast<system_process_id_t>(pid);
    process.parent_pid = static_cast<system_process_id_t>(info.pbi_ppid);
    process.name = info.pbi_name;
    if (process.name.empty())
      process.name = info.pbi_comm;
    process.executable_path = ProcessPath(pid);
    process.user = UserNameFromUid(info.pbi_uid);
    process.current_user = info.pbi_uid == current_uid;
    process.accessible = !process.executable_path.empty() ||
                         !process.user.empty() || !process.name.empty();
    processes.push_back(std::move(process));
  }

  std::sort(processes.begin(), processes.end(),
            [](const ProcessInfo &lhs, const ProcessInfo &rhs) {
              return lhs.pid < rhs.pid;
            });
  return processes;
}

// ---------------------------------------------------------------------------
// acquire_instance_lock / release_instance_lock
// Use an advisory flock on a file under the per-user shared app-data dir to
// prevent duplicate instances. The fd is marked CLOEXEC so browser child
// processes do not inherit the lock and pin it after the SDK exits.
// ---------------------------------------------------------------------------
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
    // Write current PID for diagnostics.
    ::ftruncate(fd, 0);
    std::string pid_str = std::to_string(::getpid());
    if (onWriteData) {
      onWriteData(pid_str);
    }
    ::write(fd, pid_str.c_str(), pid_str.size());
    // Keep fd open — lock is held as long as fd is open.
  } while (0);
  return fd;
}

void ISystem::release_instance_lock(const system_process_handle_t &ph) {
  if (ph >= 0) {
    ::flock(ph, LOCK_UN);
    ::close(ph);
  }
}

// ---------------------------------------------------------------------------
// LaunchRemoteDebuggingPipeChrome – macOS implementation
//
// Chrome reads CDP commands from fd 3, writes CDP responses to fd 4.
// We create two anonymous pipes and use posix_spawn file actions to dup2
// the pipe ends to exactly fds 3 and 4 in the child before exec.
// ---------------------------------------------------------------------------
bool ISystem::LaunchRemoteDebuggingPipeChrome(const stl::path& chrome,
                                              const std::vector<std::u16string>& extra_args,
                                              system_process_id_t& pid,
                                              ISystem::ChromeDevPipe& pipe_out) {
  pid = 0;
  //  cmd_pipe[0]  = read  → becomes Chrome fd 3 (reads CDP commands from parent)
  //  cmd_pipe[1]  = write → parent writes CDP commands
  //  resp_pipe[0] = read  → parent reads CDP responses
  //  resp_pipe[1] = write → becomes Chrome fd 4 (writes CDP responses to parent)
  int cmd_pipe[2]  = {-1, -1};
  int resp_pipe[2] = {-1, -1};
  bool result = false;

  do {
    if (::pipe(cmd_pipe)  < 0) break;
    if (::pipe(resp_pipe) < 0) break;

    // Build argv:  executable + fixed flags + extra_args
    const std::string proc_str = chrome.string();
    std::vector<std::string> all_args;
    //all_args.push_back("--remote-debugging-pipe");
    for (const auto& a : extra_args)
      all_args.push_back(IConv::UTF16ToUTF8(a));

    std::vector<const char*> argv;
    argv.push_back(proc_str.c_str());
    for (const auto& a : all_args)
      argv.push_back(a.c_str());
    argv.push_back(nullptr);

    // posix_spawn file actions to wire up fds 3 and 4 in the child.
    // Actions are applied in listed order before exec:
    //   1. dup2(cmd_pipe[0],  3) – fd 3 = read CDP commands
    //   2. dup2(resp_pipe[1], 4) – fd 4 = write CDP responses
    //      (dup2 into fd 4 closes the original fd-4 occupant, if any)
    //   3. close parent-side fds that shouldn't be inherited by Chrome
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, cmd_pipe[0],  3);
    posix_spawn_file_actions_adddup2(&fa, resp_pipe[1], 4);
    // Close parent-only fds – skip if already at target position (3 or 4)
    // to avoid accidentally closing fds we just set up via dup2.
    if (cmd_pipe[1]  != 3 && cmd_pipe[1]  != 4)
      posix_spawn_file_actions_addclose(&fa, cmd_pipe[1]);
    if (resp_pipe[0] != 3 && resp_pipe[0] != 4)
      posix_spawn_file_actions_addclose(&fa, resp_pipe[0]);
    // Close the originals of the ones we dup'd (if they differ from 3/4)
    if (cmd_pipe[0]  != 3 && cmd_pipe[0]  != 4)
      posix_spawn_file_actions_addclose(&fa, cmd_pipe[0]);
    if (resp_pipe[1] != 3 && resp_pipe[1] != 4)
      posix_spawn_file_actions_addclose(&fa, resp_pipe[1]);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

    pid_t child_pid = 0;
    int rc = posix_spawn(&child_pid, proc_str.c_str(),
                         &fa, &attr,
                         const_cast<char* const*>(argv.data()),
                         environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);

    if (rc != 0) break;

    // Close Chrome-side ends in the parent
    ::close(cmd_pipe[0]);   cmd_pipe[0]   = -1;
    ::close(resp_pipe[1]);  resp_pipe[1]  = -1;

    pipe_out.write_cmd_fd = cmd_pipe[1];  cmd_pipe[1]  = -1;
    pipe_out.read_resp_fd = resp_pipe[0]; resp_pipe[0] = -1;
    pid = static_cast<system_process_id_t>(child_pid);
    result = true;
  } while (0);

  // Close any remaining fds on failure
  for (int fd : {cmd_pipe[0], cmd_pipe[1], resp_pipe[0], resp_pipe[1]})
    if (fd >= 0) ::close(fd);
  return result;
}
