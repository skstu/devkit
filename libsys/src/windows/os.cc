#include <libsys.h>
#include <VersionHelpers.h>

// RtlGetVersion is the reliable, non-shimmed alternative to GetVersionEx.
// We load it dynamically so it works on every Windows version without
// requiring ntddk.h / DDK headers.
typedef LONG(WINAPI* RtlGetVersion_t)(OSVERSIONINFOEXW*);

static bool rtl_get_version(OSVERSIONINFOEXW& vi) {
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) return false;
  auto fn = reinterpret_cast<RtlGetVersion_t>(
      GetProcAddress(ntdll, "RtlGetVersion"));
  if (!fn) return false;
  ZeroMemory(&vi, sizeof(vi));
  vi.dwOSVersionInfoSize = sizeof(vi);
  return fn(&vi) == 0; // STATUS_SUCCESS
}

static std::string reg_sz(HKEY root, const char* sub, const char* val) {
  HKEY hk = nullptr;
  if (RegOpenKeyExA(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &hk) !=
      ERROR_SUCCESS)
    return {};
  char buf[512] = {};
  DWORD sz = sizeof(buf), type = 0;
  LONG rc = RegQueryValueExA(hk, val, nullptr, &type,
                             reinterpret_cast<LPBYTE>(buf), &sz);
  RegCloseKey(hk);
  return rc == ERROR_SUCCESS ? std::string(buf) : std::string{};
}

ISystem::OSInfo ISystem::GetOSInfo() {
  OSInfo info;
  info.name = "Windows";

  // ── version numbers via RtlGetVersion ──────────────────────────────────
  OSVERSIONINFOEXW vi{};
  if (rtl_get_version(vi)) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%lu.%lu.%lu",
                  vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
    info.version        = buf;
    info.kernel_version = buf;
    std::snprintf(buf, sizeof(buf), "%lu", vi.dwBuildNumber);
    info.build = buf;
  }

  // ── architecture ───────────────────────────────────────────────────────
  SYSTEM_INFO si{};
  GetNativeSystemInfo(&si);
  switch (si.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: info.arch = "x86_64"; break;
    case PROCESSOR_ARCHITECTURE_ARM64: info.arch = "arm64";  break;
    case PROCESSOR_ARCHITECTURE_ARM:   info.arch = "arm";    break;
    case PROCESSOR_ARCHITECTURE_INTEL: info.arch = "x86";    break;
    default:                           info.arch = "unknown"; break;
  }

  // ── human-readable display name ────────────────────────────────────────
  // Windows 10/11 both report MajorVersion 10; they are distinguished by
  // build number (>= 22000 → Windows 11).
  std::string product_name =
      reg_sz(HKEY_LOCAL_MACHINE,
             "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
             "ProductName");
  // On Windows 11 the registry sometimes still says "Windows 10"; fix that.
  if (vi.dwMajorVersion == 10 && vi.dwBuildNumber >= 22000) {
    const char* old_prefix = "Windows 10";
    if (product_name.rfind(old_prefix, 0) == 0)
      product_name = "Windows 11" + product_name.substr(std::strlen(old_prefix));
  }
  // Append build number for precision (e.g. "Windows 11 Pro (Build 22621)")
  if (!product_name.empty() && !info.build.empty()) {
    info.display_name = product_name + " (Build " + info.build + ")";
  } else {
    info.display_name = product_name.empty() ? info.name : product_name;
  }

  return info;
}
