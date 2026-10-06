#include <libsys.h>

#include "../device_fp.hpp"

#include <cerrno>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <ifaddrs.h>
#include <pwd.h>
#include <sstream>
#include <sys/utsname.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {
std::string ArchitectureName() {
#if defined(__aarch64__) || defined(__arm64__)
  return "arm64";
#elif defined(__arm__)
  return "arm";
#elif defined(__x86_64__)
  return "x86_64";
#elif defined(__i386__)
  return "x86";
#else
  return "unknown";
#endif
}

std::string AddressString(const sockaddr *address) {
  char buffer[INET6_ADDRSTRLEN] = {};
  if (address == nullptr)
    return {};
  if (address->sa_family == AF_INET) {
    const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(address);
    return inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer)) == nullptr
               ? std::string{}
               : std::string{buffer};
  }
  if (address->sa_family == AF_INET6) {
    const auto *ipv6 = reinterpret_cast<const sockaddr_in6 *>(address);
    return inet_ntop(AF_INET6, &ipv6->sin6_addr, buffer, sizeof(buffer)) ==
                   nullptr
               ? std::string{}
               : std::string{buffer};
  }
  return {};
}

bool IsVirtualInterface(const std::string &name) {
  return name.rfind("tun", 0) == 0 || name.rfind("tap", 0) == 0 ||
         name.rfind("utun", 0) == 0 || name.rfind("ppp", 0) == 0;
}
} // namespace

ISystem::OSInfo ISystem::GetOSInfo() {
  OSInfo info;
#if defined(__OSANDROID__)
  info.name = "Android";
#elif defined(__OSIOS__)
  info.name = "iOS";
#else
  info.name = "macOS";
#endif
  utsname system_info{};
  if (uname(&system_info) == 0) {
    info.version = system_info.release;
    info.build = system_info.version;
    info.kernel_version = system_info.release;
  }
  info.display_name = info.name;
  info.arch = ArchitectureName();
  return info;
}

std::vector<ISystem::ProcessInfo> ISystem::ListProcesses() {
  ProcessInfo process;
  process.pid = GetCurrentProcID();
  process.parent_pid = getppid();
  process.executable_path = GetCurrentProcessPath().string();
  process.name = GetCurrentProcessPath().filename().string();
  process.current_user = true;
  process.accessible = true;
  if (passwd *user = getpwuid(getuid()); user != nullptr && user->pw_name)
    process.user = user->pw_name;
  return {std::move(process)};
}

std::string ISystem::GetDeviceFingerprint() {
  const OSInfo info = GetOSInfo();
  return device_fp::make_fingerprint(
      {info.name, info.version, info.arch, GetCurrentProcessPath().string()});
}

std::vector<ISystem::GPUInfo> ISystem::GetGPUInfo() { return {}; }



stl::path ISystem::GetCurrentProcessPath() {
#if defined(__OSANDROID__)
  char path[4096] = {};
  const ssize_t size = readlink("/proc/self/exe", path, sizeof(path) - 1);
  return size <= 0 ? stl::path{} : stl::path(std::string(path, size));
#else
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> path(size);
  return _NSGetExecutablePath(path.data(), &size) == 0 ? stl::path(path.data())
                                                       : stl::path{};
#endif
}

stl::path ISystem::GetDllPath(void *symbol) {
  Dl_info info{};
  return dladdr(symbol, &info) != 0 && info.dli_fname != nullptr
             ? stl::path(info.dli_fname)
             : stl::path{};
}

bool ISystem::LaunchProcess(const stl::path &,
                            const std::vector<std::string> &,
                            system_process_id_t &pid) {
  pid = 0;
  return false;
}

bool ISystem::LaunchProcess(const stl::path &,
                            const std::vector<std::u16string> &,
                            system_process_id_t &pid) {
  pid = 0;
  return false;
}

bool ISystem::LaunchRemoteDebuggingPipeChrome(
    const stl::path &, const std::vector<std::u16string> &,
    system_process_id_t &pid, ChromeDevPipe &pipe_out) {
  pid = 0;
  pipe_out.Close();
  return false;
}

bool ISystem::KillProc(system_process_id_t, int) { return false; }

bool ISystem::HasProc(system_process_id_t pid) {
  return pid > 0 && (kill(pid, 0) == 0 || errno == EPERM);
}

bool ISystem::ActivateProcess(system_process_id_t) { return false; }

system_process_handle_t ISystem::acquire_instance_lock(
    const std::string &name, bool &exists,
    const std::function<void(std::string &)> &on_write_data) {
  exists = false;
  std::error_code error;
  const auto base = GetUserAppDataDir();
  if (base.empty()) return -1;
  const stl::path directory = base / "sovkit";
  std::filesystem::create_directories(directory, error);
  if (error)
    return -1;
  const stl::path path = directory / (name + ".lock");
  const int descriptor = open(path.c_str(), O_CREAT | O_RDWR, 0600);
  if (descriptor < 0)
    return -1;
  if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
    exists = errno == EWOULDBLOCK || errno == EAGAIN;
    close(descriptor);
    return -1;
  }
  if (on_write_data) {
    std::string data;
    on_write_data(data);
    if (!data.empty()) {
      ftruncate(descriptor, 0);
      static_cast<void>(write(descriptor, data.data(), data.size()));
    }
  }
  return descriptor;
}

void ISystem::release_instance_lock(const system_process_handle_t &handle) {
  if (handle < 0)
    return;
  flock(handle, LOCK_UN);
  close(handle);
}

bool ISystem::is_tcp_port_available(const unsigned short &port,
                                    const std::string &address,
                                    std::string *out_error) {
  const int descriptor = socket(AF_INET, SOCK_STREAM, 0);
  if (descriptor < 0)
    return false;
  sockaddr_in endpoint{};
  endpoint.sin_family = AF_INET;
  endpoint.sin_port = htons(port);
  const bool valid_address =
      inet_pton(AF_INET, address.c_str(), &endpoint.sin_addr) == 1;
  const bool available =
      valid_address && bind(descriptor, reinterpret_cast<sockaddr *>(&endpoint),
                            sizeof(endpoint)) == 0;
  if (!available && out_error != nullptr)
    *out_error = valid_address ? std::strerror(errno) : "invalid_address";
  close(descriptor);
  return available;
}

ISystem::NetEnvInfo ISystem::GetNetEnvironment() {
  NetEnvInfo info;
  ifaddrs *addresses = nullptr;
  if (getifaddrs(&addresses) == 0) {
    for (const ifaddrs *item = addresses; item != nullptr;
         item = item->ifa_next) {
      if (item->ifa_name == nullptr || item->ifa_addr == nullptr)
        continue;
      const int family = item->ifa_addr->sa_family;
      if (family != AF_INET && family != AF_INET6)
        continue;
      const std::string name(item->ifa_name);
      auto existing = std::find_if(
          info.interfaces.begin(), info.interfaces.end(),
          [&name](const NetEnvInfo::InterfaceInfo &value) {
            return value.name == name;
          });
      if (existing == info.interfaces.end()) {
        info.interfaces.push_back({});
        existing = std::prev(info.interfaces.end());
        existing->name = name;
      }
      existing->is_up = (item->ifa_flags & IFF_UP) != 0;
      existing->is_loopback = (item->ifa_flags & IFF_LOOPBACK) != 0;
      existing->is_tun = IsVirtualInterface(name);
      existing->is_vpn = existing->is_tun;
      const std::string address = AddressString(item->ifa_addr);
      if (family == AF_INET && existing->ipv4.empty())
        existing->ipv4 = address;
      if (family == AF_INET6 && existing->ipv6.empty())
        existing->ipv6 = address;
      if (existing->is_tun) {
        info.tun_vpn.tun_active = true;
        info.tun_vpn.vpn_active = true;
        info.tun_vpn.tun_ifaces.push_back(name);
      }
    }
    freeifaddrs(addresses);
  }
  info.proxy.type_str = "unknown";
  info.connection_type = info.tun_vpn.vpn_active
                             ? NetEnvInfo::ConnType::VPN
                             : NetEnvInfo::ConnType::Unknown;
  info.connection_type_str = info.tun_vpn.vpn_active ? "vpn" : "unknown";
  if (const char *locale = std::setlocale(LC_ALL, nullptr); locale != nullptr)
    info.locale.locale = locale;
  return info;
}

ISystem::EgressProbeInfo ISystem::DetectSystemEgress(uint32_t timeout_ms) {
  EgressProbeInfo probe;
  probe.timeout_ms = timeout_ms;
  probe.capability_str = "unknown";
  probe.global_error = "platform_probe_unavailable";
  probe.domestic_error = "platform_probe_unavailable";
  const NetEnvInfo environment = GetNetEnvironment();
  probe.tun_active = environment.tun_vpn.tun_active;
  probe.vpn_active = environment.tun_vpn.vpn_active;
  probe.connection_type = environment.connection_type_str;
  return probe;
}