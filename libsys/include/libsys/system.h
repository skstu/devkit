#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <libsys/platform.h>
#if !defined(__OSWIN__)
#include <sys/types.h> // pid_t is part of the existing process API.
#endif

namespace stl {
using path = std::filesystem::path;
}

#if defined(__OSWIN__)
typedef void* system_process_handle_t;
typedef unsigned long system_process_id_t;
#else
typedef int system_process_handle_t;
typedef pid_t system_process_id_t;
#endif

class ISystem {
public:
  enum class GPUVendor { Unknown, AMD, Apple, Intel, NVIDIA, Qualcomm };
  struct GPUInfo {
    GPUVendor vendor = GPUVendor::Unknown;
    std::string name;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    bool primary = false;
  };
  struct OSInfo {
    std::string name;           // "Windows" | "macOS" | "Linux"
    std::string display_name;   // "Windows 11 Pro" | "macOS Sonoma 14.3.1" |
                                // "Ubuntu 22.04.3 LTS"
    std::string version;        // "10.0.22621"   | "14.3.1"   | "22.04"
    std::string build;          // "22621"         | "23D56"    | kernel release
    std::string arch;           // "x86_64" | "arm64" | "x86"
    std::string kernel_version; // NT version / XNU release / kernel uname
  };
  struct ProcessInfo {
    system_process_id_t pid = 0;
    system_process_id_t parent_pid = 0;
    std::string name;
    std::string executable_path; // UTF-8 when available.
    std::string command_line;    // Best-effort; often unavailable cross-user.
    std::string user;            // Best-effort account name.
    bool current_user = false;
    bool accessible = false; // True when at least one detail field was readable.
  };
public:
  static OSInfo GetOSInfo();
  static std::vector<ProcessInfo> ListProcesses();
  static std::string ListProcessesToJsonString();
  static std::string ListProcessesToString();
  static std::string GetDeviceFingerprint();
  static std::vector<GPUInfo> GetGPUInfo();
  static GPUVendor GetGPUVendor();
  static const char *GPUVendorToString(GPUVendor vendor);
  static stl::path GetCurrentProcessPath();
  static stl::path GetCurrentProcessDir();
  // Persistent data base from the actual host's system APIs. Does not append
  // a product ID or create directories. Failure returns an empty path, never
  // cwd or a temporary directory. Android requires Context.filesDir from its
  // host; a desktop-supplied override is rejected.
  static stl::path GetUserAppDataDir();
  static stl::path GetUserAppDataDir(const stl::path& android_files_dir);
  static stl::path GetDllPath(void *static_dummy_variable);
  static stl::path GetDllDir(void *);
  static stl::path GetDllName(void *);
  // Captures the current thread stack. skip_frames skips callers above
  // CaptureStackTrace after the helper itself has been removed.
  static std::vector<std::string> CaptureStackTrace(std::size_t max_frames = 32,
                                                    std::size_t skip_frames = 0);
  static bool LaunchProcess(const stl::path &,
                            const std::vector<std::string> &args,
                            system_process_id_t &pid);
  static bool LaunchProcess(const stl::path &,
                            const std::vector<std::u16string> &args,
                            system_process_id_t &pid);
  // Pipe handle pair for --remote-debugging-pipe Chrome sessions.
  // fd 3 in Chrome = reads CDP commands  (parent writes via write_cmd /
  // write_cmd_fd) fd 4 in Chrome = writes CDP responses (parent reads via
  // read_resp / read_resp_fd)
  struct ChromeDevPipe {
    explicit ChromeDevPipe();
    ~ChromeDevPipe() = default;
#if defined(__OSWIN__)
    system_process_handle_t write_cmd;
    system_process_handle_t read_resp;
#else
    int write_cmd_fd;
    int read_resp_fd;
#endif
    bool Valid() const;
    void Close();
  };

  // Launch Chrome with --remote-debugging-pipe.
  // extra_args: additional flags such as --user-data-dir=..., --no-sandbox,
  // etc. On success, pipe_out.write_cmd[_fd] / pipe_out.read_resp[_fd] are set.
  static bool LaunchRemoteDebuggingPipeChrome(
      const stl::path &chrome, const std::vector<std::u16string> &extra_args,
      system_process_id_t &pid, ChromeDevPipe &pipe_out);
  static bool is_tcp_port_available(const unsigned short &port,
                                    const std::string &address = "0.0.0.0",
                                    std::string *out_err = nullptr);
  // Legacy name-scoped lock, not a workspace lock. On Android this overload
  // cannot resolve Context.filesDir and fails. For all-platform path-scoped
  // ownership use libsys::FileLock from <libsys/file_lock.h>; keep it alive.
  static system_process_handle_t acquire_instance_lock(const std::string &name,
                                                       bool &exists,const std::function<void(std::string&)>& onWriteData = nullptr);
  static void release_instance_lock(const system_process_handle_t &ph);

  static bool KillProc(system_process_id_t pid, int /*signal*/);
  static bool HasProc(system_process_id_t pid);
  static system_process_id_t GetCurrentProcID();
  // Bring the main window of the process with the given PID to the foreground.
  // Returns true if the activation request was sent (the window may still take
  // a moment to appear on top).  Returns false if no window could be found.
  static bool ActivateProcess(system_process_id_t pid);

  // ── Network environment interrogation ────────────────────────────────────
  struct NetEnvInfo {

    // ── Network interface ───────────────────────────────────────────────
    struct InterfaceInfo {
      std::string name;          // "eth0", "en0", "Ethernet"
      std::string friendly_name; // Windows-friendly display name
      std::string ipv4;          // "192.168.1.100"
      std::string ipv4_cidr;     // "192.168.1.100/24"
      std::string ipv6;          // first global/link-local IPv6 addr
      std::string mac;           // "aa:bb:cc:dd:ee:ff"
      std::string netmask;       // "255.255.255.0"
      std::string broadcast;     // "192.168.1.255"
      uint32_t mtu = 0;
      bool is_up = false;
      bool is_loopback = false;
      bool is_wireless = false;
      bool is_tun = false; // TUN/TAP virtual device
      bool is_vpn = false; // PPP / VPN virtual device
    };
    std::vector<InterfaceInfo> interfaces;
    std::string egress_interface;     // interface used for default route
    std::string egress_ip;            // local IP on that interface
    std::string default_gateway_ipv4; // "192.168.1.1"
    std::string default_gateway_ipv6;
    std::vector<std::string> dns_servers;

    // ── System proxy settings ───────────────────────────────────────────
    struct ProxyInfo {
      enum class Type { None, HTTP, HTTPS, SOCKS4, SOCKS5, PAC, AutoDetect };
      Type type = Type::None;
      std::string type_str;
      std::string host;
      uint16_t port = 0;
      std::string pac_url;
      bool auto_detect = false;
      // True only when the platform-specific settings source was read
      // successfully.  A default-initialized `None` is not proof that the
      // process has no proxy (for example, WinHTTP may be running under a
      // service account whose IE settings cannot be queried).
      bool detection_known = false;
      std::vector<std::string> bypass_list;
    };
    ProxyInfo proxy;

    // ── TUN / VPN detection ─────────────────────────────────────────────
    struct TunVpnInfo {
      bool tun_active = false;
      bool vpn_active = false;
      std::vector<std::string> tun_ifaces; // "utun0", "tun0", …
      std::vector<std::string> vpn_ifaces; // "ppp0", …
      std::string provider_name;           // VPN provider if detectable
    };
    TunVpnInfo tun_vpn;

    // ── Overall connection classification ───────────────────────────────
    enum class ConnType { Direct, Proxy, TUN, VPN, Unknown };
    ConnType connection_type = ConnType::Unknown;
    std::string connection_type_str;

    // ── System locale / timezone / language ────────────────────────────
    struct LocaleInfo {
      std::string timezone_name;  // "Asia/Shanghai", "America/New_York"
      std::string timezone_abbr;  // "CST", "PST"
      int utc_offset_sec = 0;     // seconds east of UTC (+28800 = +08:00)
      std::string utc_offset_str; // "+08:00"
      std::string language;       // "zh-CN", "en-US"
      std::string locale;         // "zh_CN.UTF-8", "en_US.UTF-8"
      std::string country_code;   // "CN", "US"
    };
    LocaleInfo locale;

    // ── TCP/IP stack parameters (egress interface) ──────────────────────
    struct TcpStackInfo {
      uint32_t recv_window_size = 0; // initial receive window (bytes)
      uint32_t send_buffer_size = 0; // send buffer / write window (bytes)
      uint32_t max_window_size = 0;  // max window after scaling
      uint32_t window_scale = 0;     // window scale exponent
      uint32_t mss = 0;              // typical MSS (bytes)
      uint32_t default_ttl = 0;      // IP TTL
      bool window_scaling = false;   // RFC 1323 window scaling
      bool sack_enabled = false;     // Selective ACK
      bool timestamps = false;       // RFC 1323 timestamps
      bool ecn_enabled = false;      // Explicit Congestion Notification
      uint32_t keepalive_time_sec = 0;
      uint32_t keepalive_intvl_sec = 0;
      uint8_t keepalive_probes = 0;
    };
    TcpStackInfo tcp_stack;

    // ── TLS fingerprint (JA3 / JA4) ────────────────────────────────────
    // Derived from the platform TLS stack configuration (SChannel /
    // SecureTransport / OpenSSL). Represents what a ClientHello from this OS
    // would look like.
    struct TlsFpInfo {
      std::string ja3_str;                 // raw JA3 input string
      std::string ja3;                     // MD5(ja3_str) hex
      std::string ja4;                     // JA4 fingerprint string
      uint16_t tls_version_max = 0;        // 0x0304=TLS1.3, 0x0303=TLS1.2, …
      std::string tls_version_str;         // "TLS 1.3"
      std::vector<uint16_t> cipher_suites; // IANA codes offered
      std::vector<uint16_t> extensions;    // extension type IDs in order
      std::vector<uint16_t> groups;        // supported_groups / curves
      std::vector<uint8_t> ec_point_fmts;  // EC point formats
      std::vector<std::string> alpn;       // ALPN protocols
    };
    TlsFpInfo tls_fp;
  };

  static NetEnvInfo GetNetEnvironment();
  static std::string GetNetEnvironmentToJsonString();

  // ── System egress probe ─────────────────────────────────────────────────
  // Lightweight preflight probe used before Chromium starts:
  //   1. Probe google.com to determine whether the current system stack can
  //      reach an overseas site.
  //   2. Probe baidu.com concurrently to distinguish "domestic only" from
  //      "offline / unknown" without adding a second timeout to startup.
  //
  // The implementation is platform-specific:
  //   • Windows: WinHTTP automatic/system proxy path
  //   • macOS  : system URL loading stack
  //   • Linux  : current system proxy config + lightweight direct/proxy probe
  //              (PAC / auto-detect / HTTPS proxy currently return Unknown)
  struct EgressProbeInfo {
    enum class Capability { Unknown, GlobalEgress, DomesticOnly, Offline };
    Capability capability = Capability::Unknown;
    std::string capability_str;

    uint32_t timeout_ms = 1200;
    std::string global_url = "http://google.com/";
    std::string domestic_url = "http://baidu.com/";

    bool global_checked = false;
    bool global_ok = false;
    int global_status = 0;
    std::string global_error;

    bool domestic_checked = false;
    bool domestic_ok = false;
    int domestic_status = 0;
    std::string domestic_error;

    bool system_proxy_present = false;
    std::string system_proxy_type;
    std::string system_proxy_host;
    uint16_t system_proxy_port = 0;
    bool proxy_auto_detect = false;
    std::string proxy_pac_url;

    bool tun_active = false;
    bool vpn_active = false;
    std::string connection_type;
  };

  static EgressProbeInfo DetectSystemEgress(uint32_t timeout_ms = 1200);
  static std::string DetectSystemEgressToJsonString(uint32_t timeout_ms = 1200);
  // SDK shutdown drains speculative platform probes before a host unloads the
  // runtime library. The reusable executor remains available for a later
  // SDK lifecycle.
  static void WaitForEgressProbeIdle();
  static bool WaitForEgressProbeIdle(uint32_t timeout_ms);

public:
  explicit ISystem() = default;
  ~ISystem() = default;
};

