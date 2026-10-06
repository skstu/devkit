#include <libsys.h>
#include "../text_encoding.hpp"
#include <sddl.h>
#include <algorithm>


namespace {
stl::path ModulePath(HMODULE module) {
  // GetModuleFileNameW reports truncation by returning the buffer capacity.
  // A fixed MAX_PATH buffer silently loaded the wrong SDK beside long paths.
  for (std::size_t capacity = MAX_PATH; capacity <= 32768;) {
    std::wstring buffer(capacity, L'\0');
    const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length) return {};
    if (length < buffer.size()) {
      buffer.resize(length);
      return stl::path(buffer);
    }
    if (capacity == 32768) break;
    capacity = (std::min)(capacity * 2, std::size_t{32768});
  }
  return {};
}
} // namespace

stl::path ISystem::GetCurrentProcessPath() { return ModulePath(nullptr); }

stl::path ISystem::GetDllPath(void* symbol) {
  if (!symbol) return {};
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(symbol), &module)) return {};
  return ModulePath(module);
}

namespace {
bool WindowsArgNeedsQuotes(const std::wstring &arg) {
  if (arg.empty())
    return true;
  for (wchar_t ch : arg) {
    if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r' ||
        ch == L'\v' || ch == L'"')
      return true;
  }
  return false;
}

std::wstring QuoteWindowsArg(const std::wstring &arg) {
  if (!WindowsArgNeedsQuotes(arg))
    return arg;

  std::wstring result;
  result.reserve(arg.size() + 2);
  result.push_back(L'"');

  size_t backslashes = 0;
  for (wchar_t ch : arg) {
    if (ch == L'\\') {
      ++backslashes;
      continue;
    }
    if (ch == L'"') {
      result.append(backslashes * 2 + 1, L'\\');
      result.push_back(ch);
      backslashes = 0;
      continue;
    }
    result.append(backslashes, L'\\');
    backslashes = 0;
    result.push_back(ch);
  }

  result.append(backslashes * 2, L'\\');
  result.push_back(L'"');
  return result;
}

std::wstring NormalizeWindowsArg(std::wstring arg) {
  if (arg.size() >= 2 && arg.front() == L'"' && arg.back() == L'"')
    arg = arg.substr(1, arg.size() - 2);

  const size_t eq = arg.find(L'=');
  if (eq != std::wstring::npos && eq + 2 < arg.size() &&
      arg[eq + 1] == L'"' && arg.back() == L'"') {
    arg.erase(eq + 1, 1);
    arg.pop_back();
  }
  return arg;
}

std::wstring BuildWindowsCommandLine(const stl::path &proc,
                                     const std::vector<std::wstring> &args) {
  std::wstring cmd = QuoteWindowsArg(proc.wstring());
  for (const auto &arg : args) {
    cmd.push_back(L' ');
    cmd.append(QuoteWindowsArg(NormalizeWindowsArg(arg)));
  }
  return cmd;
}
} // namespace

bool ISystem::LaunchProcess(const stl::path &proc,
                            const std::vector<std::string> &args,
                            system_process_id_t &pid) {
  bool result = false;
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  ZeroMemory(&pi, sizeof(pi));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  // si.wShowWindow = (show_flag == 0) ? SW_HIDE : SW_SHOW;
  si.wShowWindow = SW_SHOW;

  do {
    std::vector<std::wstring> wide_args;
    wide_args.reserve(args.size());
    for (const auto &arg : args)
      wide_args.push_back(IConv::UTF8ToWide(arg));

    std::wstring full_cmd = BuildWindowsCommandLine(proc, wide_args);

    // CreateProcess requires a writable buffer for lpCommandLine
    std::vector<wchar_t> cmd_buf(full_cmd.begin(), full_cmd.end());
    cmd_buf.push_back(L'\0');

    BOOL inherit_handles = FALSE;
    DWORD creation_flags = 0;
    // if (show_flag == 0) {
    //	creation_flags |= CREATE_NO_WINDOW;
    // }

    // Use NULL lpApplicationName and pass full command line (writable)
    BOOL status = CreateProcessW(NULL,            // lpApplicationName
                                 cmd_buf.data(),  // lpCommandLine (writable)
                                 NULL,            // lpProcessAttributes
                                 NULL,            // lpThreadAttributes
                                 inherit_handles, // bInheritHandles
                                 creation_flags,  // dwCreationFlags
                                 NULL,            // lpEnvironment
                                 NULL,            // lpCurrentDirectory
                                 &si,             // lpStartupInfo
                                 &pi);            // lpProcessInformation

    if (status == FALSE) {
      // optional: DWORD err = GetLastError();
      break;
    }

    pid = pi.dwProcessId;

    // Close returned handles to avoid leaks; process continues running.
    if (pi.hThread) {
      CloseHandle(pi.hThread);
      pi.hThread = NULL;
    }
    if (pi.hProcess) {
      CloseHandle(pi.hProcess);
      pi.hProcess = NULL;
    }

    result = true;
  } while (0);
  return result;
}
bool ISystem::LaunchProcess(const stl::path &proc,
                            const std::vector<std::u16string> &args,
                            system_process_id_t &pid) {
  bool result = false;
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  ZeroMemory(&pi, sizeof(pi));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  // si.wShowWindow = (show_flag == 0) ? SW_HIDE : SW_SHOW;
  si.wShowWindow = SW_SHOW;

  do {
    std::vector<std::wstring> wide_args;
    wide_args.reserve(args.size());
    for (const auto &arg : args)
      wide_args.push_back(IConv::UTF16ToWide(arg));

    std::wstring full_cmd = BuildWindowsCommandLine(proc, wide_args);

    // CreateProcess requires a writable buffer for lpCommandLine
    std::vector<wchar_t> cmd_buf(full_cmd.begin(), full_cmd.end());
    cmd_buf.push_back(L'\0');

    BOOL inherit_handles = FALSE;
    DWORD creation_flags = 0;
    // if (show_flag == 0) {
    //	creation_flags |= CREATE_NO_WINDOW;
    // }

    // Use NULL lpApplicationName and pass full command line (writable)
    BOOL status = CreateProcessW(NULL,            // lpApplicationName
                                 cmd_buf.data(),  // lpCommandLine (writable)
                                 NULL,            // lpProcessAttributes
                                 NULL,            // lpThreadAttributes
                                 inherit_handles, // bInheritHandles
                                 creation_flags,  // dwCreationFlags
                                 NULL,            // lpEnvironment
                                 NULL,            // lpCurrentDirectory
                                 &si,             // lpStartupInfo
                                 &pi);            // lpProcessInformation

    if (status == FALSE) {
      // optional: DWORD err = GetLastError();
      break;
    }

    pid = pi.dwProcessId;

    // Close returned handles to avoid leaks; process continues running.
    if (pi.hThread) {
      CloseHandle(pi.hThread);
      pi.hThread = NULL;
    }
    if (pi.hProcess) {
      CloseHandle(pi.hProcess);
      pi.hProcess = NULL;
    }

    result = true;
  } while (0);
  return result;
}

namespace {
std::string WideToUtf8(const std::wstring &value) {
  return IConv::WideToUTF8(value);
}

std::string AccountNameFromSid(PSID sid) {
  if (!sid)
    return {};

  DWORD name_len = 0;
  DWORD domain_len = 0;
  SID_NAME_USE use = SidTypeUnknown;
  LookupAccountSidW(nullptr, sid, nullptr, &name_len, nullptr, &domain_len,
                    &use);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || name_len == 0)
    return {};

  std::wstring name(name_len, L'\0');
  std::wstring domain(domain_len, L'\0');
  if (!LookupAccountSidW(nullptr, sid, name.data(), &name_len, domain.data(),
                         &domain_len, &use))
    return {};

  name.resize(name_len);
  domain.resize(domain_len);
  if (!domain.empty())
    return WideToUtf8(domain + L"\\" + name);
  return WideToUtf8(name);
}

std::string SidStringFromToken(HANDLE token) {
  DWORD needed = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed == 0)
    return {};

  std::vector<BYTE> buffer(needed);
  if (!GetTokenInformation(token, TokenUser, buffer.data(),
                           static_cast<DWORD>(buffer.size()), &needed))
    return {};

  const auto *user = reinterpret_cast<const TOKEN_USER *>(buffer.data());
  LPWSTR sid_text = nullptr;
  if (!ConvertSidToStringSidW(user->User.Sid, &sid_text))
    return {};

  std::wstring result(sid_text);
  LocalFree(sid_text);
  return WideToUtf8(result);
}

std::string CurrentUserSidString() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return {};
  std::string sid = SidStringFromToken(token);
  CloseHandle(token);
  return sid;
}

std::string ProcessUser(HANDLE process, std::string *sid_out = nullptr) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &token))
    return {};

  DWORD needed = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed == 0) {
    CloseHandle(token);
    return {};
  }

  std::vector<BYTE> buffer(needed);
  if (!GetTokenInformation(token, TokenUser, buffer.data(),
                           static_cast<DWORD>(buffer.size()), &needed)) {
    CloseHandle(token);
    return {};
  }

  const auto *user = reinterpret_cast<const TOKEN_USER *>(buffer.data());
  std::string account = AccountNameFromSid(user->User.Sid);
  if (sid_out) {
    LPWSTR sid_text = nullptr;
    if (ConvertSidToStringSidW(user->User.Sid, &sid_text)) {
      *sid_out = WideToUtf8(std::wstring(sid_text));
      LocalFree(sid_text);
    }
  }
  CloseHandle(token);
  return account;
}

std::string ProcessImagePath(HANDLE process) {
  std::wstring path(32768, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process, 0, path.data(), &size))
    return {};
  path.resize(size);
  return WideToUtf8(path);
}
} // namespace

std::vector<ISystem::ProcessInfo> ISystem::ListProcesses() {
  std::vector<ProcessInfo> processes;
  const std::string current_sid = CurrentUserSidString();

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return processes;

  PROCESSENTRY32W entry = {};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      ProcessInfo info;
      info.pid = static_cast<system_process_id_t>(entry.th32ProcessID);
      info.parent_pid =
          static_cast<system_process_id_t>(entry.th32ParentProcessID);
      info.name = WideToUtf8(entry.szExeFile);

      HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   entry.th32ProcessID);
      if (process) {
        info.executable_path = ProcessImagePath(process);
        std::string process_sid;
        info.user = ProcessUser(process, &process_sid);
        info.current_user = !current_sid.empty() && !process_sid.empty() &&
                            current_sid == process_sid;
        info.accessible = !info.executable_path.empty() || !info.user.empty();
        CloseHandle(process);
      }
      processes.push_back(std::move(info));
    } while (Process32NextW(snapshot, &entry));
  }

  CloseHandle(snapshot);
  return processes;
}

system_process_handle_t ISystem::acquire_instance_lock(
    const std::string &name, bool &exists,
    [[maybe_unused]] const std::function<void(std::string &)> &onWriteData) {
  system_process_handle_t ph = NULL;
  exists = false;
  do {
    if (name.empty())
      break;
    ph = CreateMutexA(NULL, TRUE, name.c_str());
    if (!ph)
      break;
    if (ERROR_ALREADY_EXISTS == GetLastError()) {
      exists = true;
      break;
    }
  } while (0);
  return ph; // 返回 fd，退出前 keep 或在 shutdown 时 close(fd)
}

void ISystem::release_instance_lock(const system_process_handle_t &ph) {
  if (ph) {
    ReleaseMutex(ph);
    CloseHandle(ph);
  }
}

bool ISystem::KillProc(system_process_id_t pid, int /*signal*/) {
  bool result = false;
  HANDLE hProcess = nullptr;
  do {
    if (pid <= 4)
      break;
    hProcess = ::OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!hProcess)
      break;
    if (::TerminateProcess(hProcess, 3762) != TRUE)
      break;
    result = true;
  } while (0);
  if (hProcess) {
    CloseHandle(hProcess);
    hProcess = nullptr;
  }
  return result;
}
bool ISystem::HasProc(system_process_id_t pid) {
  bool result = false;
  HANDLE hProcess = nullptr;
  do {
    if (pid <= 0)
      break;
    hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (hProcess == nullptr)
      break;
    DWORD rWait = WaitForSingleObject(hProcess, 0);
    result = ((rWait == WAIT_OBJECT_0) ? 0 : (int)rWait) != 0;
  } while (0);
  if (hProcess) {
    CloseHandle(hProcess);
    hProcess = nullptr;
  }
  return result;
}

// ---------------------------------------------------------------------------
// ActivateProcess — bring te main window of the given PID to the foreground.
// Enumerates top-level windows to find one owned by the process, then calls
// SetForegroundWindow.
// ---------------------------------------------------------------------------
namespace {
struct FindWindowData {
  DWORD target_pid;
  HWND found_hwnd;
};
static BOOL CALLBACK FindMainWindowProc(HWND hwnd, LPARAM lParam) {
  auto *data = reinterpret_cast<FindWindowData *>(lParam);
  // Only consider visible, top-level windows with no owner (i.e. main windows).
  if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr)
    return TRUE;
  DWORD wnd_pid = 0;
  GetWindowThreadProcessId(hwnd, &wnd_pid);
  if (wnd_pid == data->target_pid) {
    data->found_hwnd = hwnd;
    return FALSE; // stop enumeration
  }
  return TRUE;
}
} // namespace

bool ISystem::ActivateProcess(system_process_id_t pid) {
  FindWindowData data{static_cast<DWORD>(pid), nullptr};
  EnumWindows(FindMainWindowProc, reinterpret_cast<LPARAM>(&data));
  if (!data.found_hwnd)
    return false;
  HWND hwnd = data.found_hwnd;
  if (IsIconic(hwnd))
    ShowWindow(hwnd, SW_RESTORE);
  SetForegroundWindow(hwnd);
  BringWindowToTop(hwnd);
  return true;
}

// ---------------------------------------------------------------------------
// LaunchRemoteDebuggingPipeChrome – Windows implementation
//
// Chrome reads CDP commands from CRT fd 3 and writes CDP responses to fd 4.
// The CRT fd table is populated from STARTUPINFOW.lpReserved2 via the MSVC
// _ioinit() blob:
//   [DWORD count]  [BYTE _osfile × count]  [intptr_t HANDLE × count]
// _FOPEN=0x01, _FPIPE=0x08 → pipe fd flag = 0x09
// ---------------------------------------------------------------------------
bool ISystem::LaunchRemoteDebuggingPipeChrome(
    const stl::path &chrome, const std::vector<std::u16string> &extra_args,
    system_process_id_t &pid, ISystem::ChromeDevPipe &pipe_out) {
  pid = 0;
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE}; // inherit = true
  HANDLE h_cmd_rd = INVALID_HANDLE_VALUE;            // Chrome reads (fd 3)
  HANDLE h_cmd_wr = INVALID_HANDLE_VALUE;  // parent writes CDP commands
  HANDLE h_resp_rd = INVALID_HANDLE_VALUE; // parent reads CDP responses
  HANDLE h_resp_wr = INVALID_HANDLE_VALUE; // Chrome writes (fd 4)
  PROCESS_INFORMATION pi{};
  bool result = false;

  do {
    if (!CreatePipe(&h_cmd_rd, &h_cmd_wr, &sa, 0))
      break;
    if (!CreatePipe(&h_resp_rd, &h_resp_wr, &sa, 0))
      break;

    // Parent-side ends must NOT be inherited by Chrome
    SetHandleInformation(h_cmd_wr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(h_resp_rd, HANDLE_FLAG_INHERIT, 0);

    // Build lpReserved2 blob: maps CRT fds 0-4 to HANDLE values.
    // Layout (packed, no alignment padding):
    //   [DWORD count][BYTE flags×count][intptr_t handle×count]
    static constexpr DWORD kN = 5; // fds 0..4
    const DWORD blobSize =
        sizeof(DWORD) + kN * sizeof(BYTE) + kN * sizeof(intptr_t);
    std::vector<BYTE> blob(blobSize, 0);
    BYTE *p = blob.data();
    BYTE *pflags = p + sizeof(DWORD); // offset 4
    BYTE *phandles = pflags + kN;     // offset 9 (use memcpy – unaligned)

    memcpy(p, &kN, sizeof(DWORD));

    const HANDLE hs[kN] = {
        GetStdHandle(STD_INPUT_HANDLE),  // fd 0
        GetStdHandle(STD_OUTPUT_HANDLE), // fd 1
        GetStdHandle(STD_ERROR_HANDLE),  // fd 2
        h_cmd_rd,                        // fd 3 – Chrome reads CDP commands
        h_resp_wr,                       // fd 4 – Chrome writes CDP responses
    };
    // _FOPEN=0x01   _FOPEN|_FPIPE=0x09
    const BYTE flags[kN] = {0x01, 0x01, 0x01, 0x09, 0x09};
    memcpy(pflags, flags, kN);
    for (DWORD i = 0; i < kN; ++i) {
      intptr_t h = reinterpret_cast<intptr_t>(hs[i]);
      memcpy(phandles + i * sizeof(intptr_t), &h, sizeof(intptr_t));
    }

    std::vector<std::wstring> wide_args;
    wide_args.reserve(extra_args.size());
    for (const auto &a : extra_args)
      wide_args.push_back(IConv::UTF16ToWide(a));

    std::wstring cmd = BuildWindowsCommandLine(chrome, wide_args);
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;
    si.cbReserved2 = static_cast<WORD>(blobSize);
    si.lpReserved2 = blob.data();

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
                        TRUE, // bInheritHandles
                        0, nullptr, nullptr, &si, &pi))
      break;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess); // We use KillProc(pid) later; no need to keep.

    // Close Chrome-side handles in the parent (Chrome has its own copies via
    // lpReserved2)
    CloseHandle(h_cmd_rd);
    h_cmd_rd = INVALID_HANDLE_VALUE;
    CloseHandle(h_resp_wr);
    h_resp_wr = INVALID_HANDLE_VALUE;

    // Transfer ownership to caller
    pipe_out.write_cmd = h_cmd_wr;
    h_cmd_wr = INVALID_HANDLE_VALUE;
    pipe_out.read_resp = h_resp_rd;
    h_resp_rd = INVALID_HANDLE_VALUE;
    pid = pi.dwProcessId;
    result = true;
  } while (0);

  if (!result) {
    if (h_cmd_rd != INVALID_HANDLE_VALUE)
      CloseHandle(h_cmd_rd);
    if (h_cmd_wr != INVALID_HANDLE_VALUE)
      CloseHandle(h_cmd_wr);
    if (h_resp_rd != INVALID_HANDLE_VALUE)
      CloseHandle(h_resp_rd);
    if (h_resp_wr != INVALID_HANDLE_VALUE)
      CloseHandle(h_resp_wr);
    if (pi.hProcess)
      CloseHandle(pi.hProcess);
    if (pi.hThread)
      CloseHandle(pi.hThread);
  }
  return result;
}
