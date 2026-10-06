#include <libsys.h>
#include "../device_fp.hpp"

#include <dirent.h>
#include <fstream>
#include <sstream>

// ─── helpers ──────────────────────────────────────────────────────────────────

// Read the first non-empty line from a file; returns empty on failure.
static std::string read_file_line(const char* path) {
  std::ifstream f(path);
  if (!f.is_open()) return {};
  std::string line;
  while (std::getline(f, line)) {
    // trim leading/trailing whitespace
    auto b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) continue;
    auto e = line.find_last_not_of(" \t\r\n");
    return line.substr(b, e - b + 1);
  }
  return {};
}

// Read the CPU model name from /proc/cpuinfo.
static std::string cpu_model() {
  std::ifstream f("/proc/cpuinfo");
  if (!f.is_open()) return {};
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("model name", 0) == 0) {
      auto pos = line.find(':');
      if (pos != std::string::npos) {
        auto b = line.find_first_not_of(" \t", pos + 1);
        return b == std::string::npos ? std::string{} : line.substr(b);
      }
    }
  }
  return {};
}

// Return the MAC address of the first real (non-loopback, non-virtual)
// network interface found in /sys/class/net/.
static std::string first_mac_address() {
  DIR* d = opendir("/sys/class/net");
  if (!d) return {};
  std::string result;
  struct dirent* ent;
  while ((ent = readdir(d)) != nullptr) {
    std::string name = ent->d_name;
    if (name == "." || name == ".." || name == "lo") continue;
    // skip virtual interfaces (sit*, tun*, tap*, veth*, virbr*, docker*, ...)
    const char* virt[] = {"sit", "tun", "tap", "veth", "virbr",
                          "docker", "br-", "vmnet", nullptr};
    bool skip = false;
    for (int i = 0; virt[i]; ++i)
      if (name.rfind(virt[i], 0) == 0) { skip = true; break; }
    if (skip) continue;
    std::string mac = read_file_line(
        ("/sys/class/net/" + name + "/address").c_str());
    if (mac.size() == 17 && mac != "00:00:00:00:00:00") {
      result = mac;
      break;
    }
  }
  closedir(d);
  return result;
}

// ─── implementation ───────────────────────────────────────────────────────────

std::string ISystem::GetDeviceFingerprint() {
  std::vector<std::string> parts;
  parts.reserve(7);

  // 1. /etc/machine-id – stable unique ID assigned at first boot
  //    (falls back to /var/lib/dbus/machine-id on older systems)
  std::string mid = read_file_line("/etc/machine-id");
  if (mid.empty())
    mid = read_file_line("/var/lib/dbus/machine-id");
  parts.push_back(mid);

  // 2. CPU model string
  parts.push_back(cpu_model());

  // 3. Product UUID (DMI – may require root; empty on failure)
  parts.push_back(read_file_line("/sys/class/dmi/id/product_uuid"));

  // 4. Motherboard serial (DMI)
  parts.push_back(read_file_line("/sys/class/dmi/id/board_serial"));

  // 5. System product name
  parts.push_back(read_file_line("/sys/class/dmi/id/product_name"));

  // 6. First physical MAC address
  parts.push_back(first_mac_address());

  // 7. Chassis serial
  parts.push_back(read_file_line("/sys/class/dmi/id/chassis_serial"));

  return device_fp::make_fingerprint(parts);
}
