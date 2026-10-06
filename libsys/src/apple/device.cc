#include <libsys.h>
#include "../device_fp.hpp"

#include <IOKit/IOKitLib.h>
#include <sys/sysctl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>

// ─── helpers ──────────────────────────────────────────────────────────────────

// Read a string property from the IOPlatformExpertDevice registry entry.
static std::string iokit_platform_string(CFStringRef key) {
  mach_port_t iokit_port;
  if (__builtin_available(macOS 12.0, *))
    iokit_port = kIOMainPortDefault;
  else
    iokit_port = (mach_port_t)0; // kIOMasterPortDefault == 0
  io_registry_entry_t root =
      IORegistryEntryFromPath(iokit_port, "IOService:/");
  if (!root) return {};
  CFTypeRef val = IORegistryEntryCreateCFProperty(
      root, key, kCFAllocatorDefault, 0);
  IOObjectRelease(root);
  if (!val) return {};
  std::string result;
  if (CFGetTypeID(val) == CFStringGetTypeID()) {
    char buf[256] = {};
    CFStringGetCString(static_cast<CFStringRef>(val),
                       buf, sizeof(buf), kCFStringEncodingUTF8);
    result = buf;
  }
  CFRelease(val);
  return result;
}

// Read a sysctl string value.
static std::string sysctl_string(const char* name) {
  char buf[256] = {};
  size_t sz = sizeof(buf);
  sysctlbyname(name, buf, &sz, nullptr, 0);
  return std::string(buf);
}

// Return MAC of the first non-loopback, non-link-local physical interface.
static std::string first_mac_address() {
  struct ifaddrs* ifap = nullptr;
  if (getifaddrs(&ifap) != 0) return {};
  std::string result;
  for (struct ifaddrs* p = ifap; p; p = p->ifa_next) {
    if (!p->ifa_addr || p->ifa_addr->sa_family != AF_LINK) continue;
    if (p->ifa_flags & IFF_LOOPBACK) continue;
    if (!(p->ifa_flags & IFF_UP))    continue;
    auto* sdl = reinterpret_cast<struct sockaddr_dl*>(p->ifa_addr);
    if (sdl->sdl_alen != 6) continue;
    const unsigned char* m =
        reinterpret_cast<const unsigned char*>(LLADDR(sdl));
    // skip all-zero MACs
    if (!m[0] && !m[1] && !m[2] && !m[3] && !m[4] && !m[5]) continue;
    char mac[18];
    std::snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                  m[0], m[1], m[2], m[3], m[4], m[5]);
    result = mac;
    break;
  }
  freeifaddrs(ifap);
  return result;
}

// ─── implementation ───────────────────────────────────────────────────────────

std::string ISystem::GetDeviceFingerprint() {
  std::vector<std::string> parts;
  parts.reserve(7);

  // 1. IOPlatformUUID – unique per machine, stable across reboots
  parts.push_back(iokit_platform_string(CFSTR("IOPlatformUUID")));

  // 2. IOPlatformSerialNumber – hardware serial number
  parts.push_back(iokit_platform_string(CFSTR("IOPlatformSerialNumber")));

  // 3. CPU brand string
  parts.push_back(sysctl_string("machdep.cpu.brand_string"));

  // 4. Hardware model (e.g. "MacBookPro18,1")
  parts.push_back(sysctl_string("hw.model"));

  // 5. Number of logical CPUs (contributes to uniqueness on Apple Silicon)
  {
    int ncpu = 0;
    size_t sz = sizeof(ncpu);
    sysctlbyname("hw.logicalcpu", &ncpu, &sz, nullptr, 0);
    parts.push_back(ncpu ? std::to_string(ncpu) : std::string{});
  }

  // 6. First physical MAC address
  parts.push_back(first_mac_address());

  // 7. Physical memory (contributes device uniqueness; stable field)
  {
    uint64_t mem = 0;
    size_t sz = sizeof(mem);
    sysctlbyname("hw.memsize", &mem, &sz, nullptr, 0);
    parts.push_back(mem ? std::to_string(mem) : std::string{});
  }

  return device_fp::make_fingerprint(parts);
}
