#include <libsys.hpp>
#include "egress_probe_executor.hpp"
#include <nlohmann/json.hpp>
void ISystem::WaitForEgressProbeIdle() {
    libsys::detail::GetEgressProbeExecutor().WaitForIdle();
}

bool ISystem::WaitForEgressProbeIdle(uint32_t timeout_ms) {
    return libsys::detail::GetEgressProbeExecutor().WaitForIdleFor(
        std::chrono::milliseconds(timeout_ms));
}

stl::path ISystem::GetCurrentProcessDir() {
    return ISystem::GetCurrentProcessPath().parent_path();
}
stl::path ISystem::GetDllDir(void* t) {
    return ISystem::GetDllPath(t).parent_path();
}
stl::path ISystem::GetDllName(void* t) {
    return ISystem::GetDllPath(t).stem();
}
system_process_id_t ISystem::GetCurrentProcID() {
#if defined(_WIN32)
    return ::GetCurrentProcessId();
#else
    return static_cast<system_process_id_t>(::getpid());
#endif
}

const char* ISystem::GPUVendorToString(GPUVendor vendor) {
    switch (vendor) {
    case GPUVendor::AMD:
        return "amd";
    case GPUVendor::Apple:
        return "apple";
    case GPUVendor::Intel:
        return "intel";
    case GPUVendor::NVIDIA:
        return "nvidia";
    case GPUVendor::Qualcomm:
        return "qualcomm";
    default:
        return "unknown";
    }
}

ISystem::GPUVendor ISystem::GetGPUVendor() {
    const auto devices = GetGPUInfo();
    for (const auto& device : devices) {
        if (device.primary)
            return device.vendor;
    }
    return devices.empty() ? GPUVendor::Unknown : devices.front().vendor;
}

namespace {
    std::string CompactProcessName(std::string name) {
        for (char& ch : name) {
            if (ch == ';' || ch == ',' || ch == '\r' || ch == '\n' || ch == '\t')
                ch = ' ';
        }
        const auto begin = name.find_first_not_of(' ');
        if (begin == std::string::npos)
            return "unknown";
        const auto end = name.find_last_not_of(' ');
        name = name.substr(begin, end - begin + 1);
        return name.empty() ? "unknown" : name;
    }
} // namespace

std::string ISystem::ListProcessesToJsonString() {
    const auto processes = ISystem::ListProcesses();
    auto doc = nlohmann::ordered_json::object();

    auto items = nlohmann::ordered_json::array();
    for (const auto& process : processes) {
        auto item = nlohmann::ordered_json::object();
        item["pid"] = static_cast<std::int64_t>(process.pid);
        item["parentPid"] = static_cast<std::int64_t>(process.parent_pid);
        item["name"] = process.name;
        item["executablePath"] = process.executable_path;
        item["commandLine"] = process.command_line;
        item["user"] = process.user;
        item["currentUser"] = process.current_user;
        item["accessible"] = process.accessible;
        items.push_back(item);
    }

    doc["count"] = static_cast<std::uint64_t>(processes.size());
    doc["processes"] = items;
    return doc.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
}

std::string ISystem::ListProcessesToString() {
    const auto processes = ISystem::ListProcesses();
    std::string out = "!" + std::to_string(processes.size()) + ";";
    for (const auto& process : processes) {
        out += CompactProcessName(process.name);
        out += ",";
        out += std::to_string(static_cast<std::int64_t>(process.pid));
        out += ";";
    }
    return out;
}

ISystem::ChromeDevPipe::ChromeDevPipe() {
#if defined(__OSWIN__)
    // Assign to member variables.  The original code accidentally DECLARED
    // new local variables with the same names (HANDLE write_cmd = ...) which
    // shadowed the struct members and left them uninitialised (0xCDCDCDCD in
    // MSVC debug builds).  CloseHandle(0xCDCDCDCD) crashed with
    // ERROR_INVALID_HANDLE every time Close() was called on a browser that
    // had not yet reached Open() (e.g. interrupted during BootInternal).
    write_cmd = INVALID_HANDLE_VALUE;
    read_resp = INVALID_HANDLE_VALUE;
#else
    write_cmd_fd = -1;
    read_resp_fd = -1;
#endif
}
bool ISystem::ChromeDevPipe::Valid() const {
#if defined(__OSWIN__)
    return write_cmd != INVALID_HANDLE_VALUE && read_resp != INVALID_HANDLE_VALUE;
#else
    return write_cmd_fd >= 0 && read_resp_fd >= 0;
#endif
}
void ISystem::ChromeDevPipe::Close() {
#if defined(__OSWIN__)
    if (write_cmd != INVALID_HANDLE_VALUE) {
        CloseHandle(write_cmd);
        write_cmd = INVALID_HANDLE_VALUE;
    }
    if (read_resp != INVALID_HANDLE_VALUE) {
        CloseHandle(read_resp);
        read_resp = INVALID_HANDLE_VALUE;
    }
#else
    if (write_cmd_fd >= 0) {
        ::close(write_cmd_fd);
        write_cmd_fd = -1;
    }
    if (read_resp_fd >= 0) {
        ::close(read_resp_fd);
        read_resp_fd = -1;
    }
#endif
}
/*
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
*/
std::string ISystem::GetNetEnvironmentToJsonString() {
    std::string result;
    auto sysNetEnvInfo = ISystem::GetNetEnvironment();
    auto doc = nlohmann::ordered_json::object();

    auto networkInterface = nlohmann::ordered_json::array();
    for (const auto& it : sysNetEnvInfo.interfaces) {
        auto obj = nlohmann::ordered_json::object();
        obj["name"] = it.name;
        obj["friendlyName"] = it.friendly_name;
        obj["ipv4"] = it.ipv4;
        obj["ipv4Cidr"] = it.ipv4_cidr;
        obj["ipv6"] = it.ipv6;
        obj["mac"] = it.mac;
        obj["netMask"] = it.netmask;
        obj["broadcast"] = it.broadcast;
        obj["mtu"] = it.mtu;
        obj["isUp"] = it.is_up;
        obj["isLoopback"] = it.is_loopback;
        obj["isWireless"] = it.is_wireless;
        obj["isTUN"] = it.is_tun;
        obj["isVPN"] = it.is_vpn;
        networkInterface.push_back(obj);
    }
    doc["networkInterface"] = networkInterface;

    doc["egressInterface"] = sysNetEnvInfo.egress_interface;
    doc["egressIp"] = sysNetEnvInfo.egress_ip;
    doc["defaultGatewayIpv4"] = sysNetEnvInfo.default_gateway_ipv4;
    doc["defaultGatewayIpv6"] = sysNetEnvInfo.default_gateway_ipv6;

    auto dnsServers = nlohmann::ordered_json::array();
    for (const auto& it : sysNetEnvInfo.dns_servers) {
        dnsServers.push_back(it);
    }

    doc["dnsServers"] = dnsServers;

    auto proxyInfo = nlohmann::ordered_json::object();
    proxyInfo["type"] = sysNetEnvInfo.proxy.type_str;
    proxyInfo["host"] = sysNetEnvInfo.proxy.host;
    proxyInfo["port"] = sysNetEnvInfo.proxy.port;
    proxyInfo["pacUrl"] = sysNetEnvInfo.proxy.pac_url;
    proxyInfo["autoDetect"] = sysNetEnvInfo.proxy.auto_detect;
    proxyInfo["detectionKnown"] = sysNetEnvInfo.proxy.detection_known;
    auto proxyInfo_bypass_list = nlohmann::ordered_json::array();
    for (const auto& it : sysNetEnvInfo.proxy.bypass_list) {
        proxyInfo_bypass_list.push_back(it);
    }
    proxyInfo["bypassList"] = proxyInfo_bypass_list;

    doc["proxyInfo"] = proxyInfo;

    auto tunVpnInfo = nlohmann::ordered_json::object();
    tunVpnInfo["tunActive"] = sysNetEnvInfo.tun_vpn.tun_active;
    tunVpnInfo["vpnActive"] = sysNetEnvInfo.tun_vpn.vpn_active;
    tunVpnInfo["providerName"] = sysNetEnvInfo.tun_vpn.provider_name;
    auto tunVpnInfo_TunIfaces = nlohmann::ordered_json::array();
    for (const auto& it : sysNetEnvInfo.tun_vpn.tun_ifaces) {
        tunVpnInfo_TunIfaces.push_back(it);
    }
    auto tunVpnInfo_VpnIfaces = nlohmann::ordered_json::array();
    for (const auto& it : sysNetEnvInfo.tun_vpn.vpn_ifaces) {
        tunVpnInfo_VpnIfaces.push_back(it);
    }
    tunVpnInfo["tunIfaces"] = tunVpnInfo_TunIfaces;
    tunVpnInfo["vpnIfaces"] = tunVpnInfo_VpnIfaces;

    doc["tunVpn"] = tunVpnInfo;

    doc["connectionType"] = sysNetEnvInfo.connection_type_str;

    auto localeInfo = nlohmann::ordered_json::object();
    localeInfo["timezoneName"] = sysNetEnvInfo.locale.timezone_name;
    localeInfo["timezoneAbbr"] = sysNetEnvInfo.locale.timezone_abbr;
    localeInfo["utcOffsetSec"] = sysNetEnvInfo.locale.utc_offset_sec;
    localeInfo["utcOffsetStr"] = sysNetEnvInfo.locale.utc_offset_str;
    localeInfo["language"] = sysNetEnvInfo.locale.language;
    localeInfo["locale"] = sysNetEnvInfo.locale.locale;
    localeInfo["countryCode"] = sysNetEnvInfo.locale.country_code;

    doc["localeInfo"] = localeInfo;

    auto tcpStackInfo = nlohmann::ordered_json::object();
    tcpStackInfo["recvWindowSize"] = sysNetEnvInfo.tcp_stack.recv_window_size;
    tcpStackInfo["sendBufferSize"] = sysNetEnvInfo.tcp_stack.send_buffer_size;
    tcpStackInfo["maxWindowSize"] = sysNetEnvInfo.tcp_stack.max_window_size;
    tcpStackInfo["windowScale"] = sysNetEnvInfo.tcp_stack.window_scale;
    tcpStackInfo["mss"] = sysNetEnvInfo.tcp_stack.mss;
    tcpStackInfo["defaultTTL"] = sysNetEnvInfo.tcp_stack.default_ttl;
    tcpStackInfo["windowScaling"] = sysNetEnvInfo.tcp_stack.window_scaling;
    tcpStackInfo["sackEnabled"] = sysNetEnvInfo.tcp_stack.sack_enabled;
    tcpStackInfo["timestamps"] = sysNetEnvInfo.tcp_stack.timestamps;
    tcpStackInfo["ecnEnabled"] = sysNetEnvInfo.tcp_stack.ecn_enabled;
    tcpStackInfo["keepaliveTimeSec"] = sysNetEnvInfo.tcp_stack.keepalive_time_sec;
    tcpStackInfo["keepaliveIntvlSec"] = sysNetEnvInfo.tcp_stack.keepalive_intvl_sec;
    tcpStackInfo["keepaliveProbes"] = sysNetEnvInfo.tcp_stack.keepalive_probes;
    doc["tcpStackInfo"] = tcpStackInfo;

    auto tlsFpInfo = nlohmann::ordered_json::object();
    tlsFpInfo["ja3"] = sysNetEnvInfo.tls_fp.ja3;
    tlsFpInfo["ja4"] = sysNetEnvInfo.tls_fp.ja4;
    tlsFpInfo["tlsVersionMax"] = sysNetEnvInfo.tls_fp.tls_version_max;
    tlsFpInfo["tlsVersionStr"] = sysNetEnvInfo.tls_fp.tls_version_str;

    auto cipherSuites = nlohmann::ordered_json::array();
    for (const auto& it : sysNetEnvInfo.tls_fp.cipher_suites) {
        cipherSuites.push_back(it);
    }
    tlsFpInfo["cipherSuites"] = cipherSuites;

    auto extensions = nlohmann::ordered_json::array();
    for (const auto& it : sysNetEnvInfo.tls_fp.extensions) {
        extensions.push_back(it);
    }
    tlsFpInfo["extensions"] = extensions;

    auto groups = nlohmann::ordered_json::array();
    for (const auto& it : sysNetEnvInfo.tls_fp.groups) {
        groups.push_back(it);
    }
    tlsFpInfo["groups"] = groups;

    auto ecPointFmts = nlohmann::ordered_json::array();
    for (const auto& it : sysNetEnvInfo.tls_fp.ec_point_fmts) {
        ecPointFmts.push_back(it);
    }
    tlsFpInfo["ecPointFmts"] = ecPointFmts;

    auto alpns = nlohmann::ordered_json::array();
    for (const auto& it : sysNetEnvInfo.tls_fp.alpn) {
        alpns.push_back(it);
    }
    tlsFpInfo["alpn"] = alpns;

    doc["tlsFpInfo"] = tlsFpInfo;
    result = doc.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
    return result;
}

namespace {
    const char* EgressCapabilityName(
        ISystem::EgressProbeInfo::Capability capability) {
        switch (capability) {
        case ISystem::EgressProbeInfo::Capability::GlobalEgress:
            return "GlobalEgress";
        case ISystem::EgressProbeInfo::Capability::DomesticOnly:
            return "DomesticOnly";
        case ISystem::EgressProbeInfo::Capability::Offline:
            return "Offline";
        default:
            return "Unknown";
        }
    }
} // namespace

std::string ISystem::DetectSystemEgressToJsonString(uint32_t timeout_ms) {
    auto probe = DetectSystemEgress(timeout_ms);
    probe.capability_str = EgressCapabilityName(probe.capability);

    auto doc = nlohmann::ordered_json::object();

    doc["capability"] = probe.capability_str;
    doc["timeoutMs"] = probe.timeout_ms;
    doc["globalUrl"] = probe.global_url;
    doc["domesticUrl"] = probe.domestic_url;

    auto global = nlohmann::ordered_json::object();
    global["checked"] = probe.global_checked;
    global["ok"] = probe.global_ok;
    global["status"] = probe.global_status;
    global["error"] = probe.global_error;
    doc["globalProbe"] = global;

    auto domestic = nlohmann::ordered_json::object();
    domestic["checked"] = probe.domestic_checked;
    domestic["ok"] = probe.domestic_ok;
    domestic["status"] = probe.domestic_status;
    domestic["error"] = probe.domestic_error;
    doc["domesticProbe"] = domestic;

    auto proxy = nlohmann::ordered_json::object();
    proxy["present"] = probe.system_proxy_present;
    proxy["type"] = probe.system_proxy_type;
    proxy["host"] = probe.system_proxy_host;
    proxy["port"] = probe.system_proxy_port;
    proxy["autoDetect"] = probe.proxy_auto_detect;
    proxy["pacUrl"] = probe.proxy_pac_url;
    doc["systemProxy"] = proxy;

    doc["tunActive"] = probe.tun_active;
    doc["vpnActive"] = probe.vpn_active;
    doc["connectionType"] = probe.connection_type;

    return doc.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
}
