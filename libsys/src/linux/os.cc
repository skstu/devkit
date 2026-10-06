#include <libsys.h>
#include <sys/utsname.h>
#include <fstream>
#include <sstream>
#include <unordered_map>

// Parse /etc/os-release (or /usr/lib/os-release as fallback) into a key=value map.
static std::unordered_map<std::string, std::string> parse_os_release() {
  std::unordered_map<std::string, std::string> m;
  const char* paths[] = {"/etc/os-release", "/usr/lib/os-release"};
  std::ifstream f;
  for (auto* p : paths) {
    f.open(p);
    if (f.is_open()) break;
  }
  if (!f.is_open()) return m;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    // strip surrounding quotes
    if (val.size() >= 2 &&
        ((val.front() == '"' && val.back() == '"') ||
         (val.front() == '\'' && val.back() == '\'')))
      val = val.substr(1, val.size() - 2);
    m[key] = val;
  }
  return m;
}

ISystem::OSInfo ISystem::GetOSInfo() {
  OSInfo info;
  info.name = "Linux";
  std::string distro_name = "Linux";

  // ── kernel info via uname ───────────────────────────────────────────────
  struct utsname uts{};
  if (uname(&uts) == 0) {
    if (uts.sysname[0] != '\0') info.name = uts.sysname;
    info.kernel_version = uts.release;  // e.g. "6.5.0-35-generic"
    info.build          = uts.release;
    info.arch           = uts.machine;  // e.g. "x86_64", "aarch64"
  }

  // ── distribution info from /etc/os-release ─────────────────────────────
  auto rel = parse_os_release();

  // NAME or ID as the distro name for human-readable display.
  auto it = rel.find("NAME");
  if (it != rel.end() && !it->second.empty()) {
    distro_name = it->second;
  } else {
    it = rel.find("ID");
    if (it != rel.end() && !it->second.empty()) distro_name = it->second;
  }

  // VERSION_ID (e.g. "22.04", "11", "39")
  it = rel.find("VERSION_ID");
  if (it != rel.end()) info.version = it->second;

  // PRETTY_NAME is the most human-readable field
  it = rel.find("PRETTY_NAME");
  if (it != rel.end() && !it->second.empty()) {
    info.display_name = it->second;
  } else {
    // Compose fallback: NAME + VERSION
    info.display_name = distro_name;
    if (!info.version.empty())
      info.display_name += " " + info.version;
  }
  // Append kernel version for completeness
  if (!info.kernel_version.empty())
    info.display_name += " (" + info.kernel_version + ")";

  return info;
}
