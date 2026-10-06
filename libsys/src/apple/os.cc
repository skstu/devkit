#include <libsys.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

static std::string sysctl_str(const char* name) {
  char buf[256] = {};
  size_t sz = sizeof(buf);
  sysctlbyname(name, buf, &sz, nullptr, 0);
  return std::string(buf);
}

// Map macOS product version major to marketing name.
static const char* macos_name(int major) {
  switch (major) {
    case 15: return "Sequoia";
    case 14: return "Sonoma";
    case 13: return "Ventura";
    case 12: return "Monterey";
    case 11: return "Big Sur";
    case 10: return "Catalina / earlier";
    default: return "";
  }
}

ISystem::OSInfo ISystem::GetOSInfo() {
  OSInfo info;
  info.name = "macOS";

  // ── product version (e.g. "14.3.1") ────────────────────────────────────
  info.version = sysctl_str("kern.osproductversion");

  // ── build number (e.g. "23D56") ─────────────────────────────────────────
  info.build = sysctl_str("kern.osversion");

  // ── XNU kernel release (e.g. "23.3.0") ──────────────────────────────────
  struct utsname uts{};
  if (uname(&uts) == 0) {
    info.kernel_version = uts.release; // XNU version
    // arch: utsname.machine gives "arm64" / "x86_64"
    info.arch = uts.machine;
  } else {
    // fallback arch via sysctl
    int cputype = 0;
    size_t sz = sizeof(cputype);
    sysctlbyname("hw.cputype", &cputype, &sz, nullptr, 0);
    // CPU_TYPE_ARM64 = 0x0100000C, CPU_TYPE_X86_64 = 0x01000007
    if ((cputype & 0xFF) == 12)     info.arch = "arm64";
    else if ((cputype & 0xFF) == 7) info.arch = "x86_64";
    else                             info.arch = "unknown";
  }

  // ── human-readable display name ─────────────────────────────────────────
  // Parse major from version string to get the marketing name.
  int major = 0;
  if (!info.version.empty())
    major = std::atoi(info.version.c_str());
  const char* codename = macos_name(major);
  info.display_name = "macOS";
  if (codename && codename[0])
    info.display_name += std::string(" ") + codename;
  if (!info.version.empty())
    info.display_name += " " + info.version;
  if (!info.build.empty())
    info.display_name += " (" + info.build + ")";

  return info;
}

