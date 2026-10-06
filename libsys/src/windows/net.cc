#include <libsys.h>
#include "../net_env_impl.hpp"

// Additional Windows headers needed for network interrogation
#include <iphlpapi.h>       // GetAdaptersAddresses, GetIpForwardTable2
#include <netioapi.h>       // GetBestRoute2, ConvertInterfaceLuidToAlias
#include <winhttp.h>        // WinHttpGetIEProxyConfigForCurrentUser
#include <ws2tcpip.h>
#include <windns.h>         // DNS_TYPE_*

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "dnsapi.lib")

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Convert an IPv4/IPv6 sockaddr to a dotted-decimal / colon-hex string.
static std::string sockaddr_to_str(const SOCKADDR* sa) {
    char buf[128] = {};
    if (!sa) return {};
    if (sa->sa_family == AF_INET) {
        inet_ntop(AF_INET,
            &reinterpret_cast<const SOCKADDR_IN*>(sa)->sin_addr,
            buf, sizeof(buf));
    } else if (sa->sa_family == AF_INET6) {
        inet_ntop(AF_INET6,
            &reinterpret_cast<const SOCKADDR_IN6*>(sa)->sin6_addr,
            buf, sizeof(buf));
    }
    return buf;
}

static std::string wide_to_utf8(const wchar_t* value) {
    if (!value || !*value) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size,
                            nullptr, nullptr) <= 0)
        return {};
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

// Retrieve a REG_MULTI_SZ value as vector<std::string>.
static std::vector<std::string> reg_multi_sz(HKEY root, const char* sub,
                                              const char* val) {
    std::vector<std::string> result;
    HKEY hk = nullptr;
    if (RegOpenKeyExA(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &hk) != ERROR_SUCCESS)
        return result;
    DWORD type = 0, sz = 0;
    if (RegQueryValueExA(hk, val, nullptr, &type, nullptr, &sz) != ERROR_SUCCESS
        || type != REG_MULTI_SZ || sz == 0) {
        RegCloseKey(hk); return result;
    }
    std::vector<char> buf(sz);
    if (RegQueryValueExA(hk, val, nullptr, &type,
                         reinterpret_cast<LPBYTE>(buf.data()), &sz) == ERROR_SUCCESS) {
        const char* p = buf.data();
        while (*p) {
            result.emplace_back(p);
            p += strlen(p) + 1;
        }
    }
    RegCloseKey(hk);
    return result;
}

static DWORD reg_dword(HKEY root, const char* sub, const char* val,
                        DWORD def = 0) {
    HKEY hk = nullptr;
    if (RegOpenKeyExA(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &hk) != ERROR_SUCCESS)
        return def;
    DWORD v = def, sz = sizeof(v), type = 0;
    RegQueryValueExA(hk, val, nullptr, &type, reinterpret_cast<LPBYTE>(&v), &sz);
    RegCloseKey(hk);
    return v;
}

// Prefix length → dotted netmask string
static std::string prefix_to_netmask(uint8_t prefix) {
    uint32_t mask = prefix ? (~0u << (32 - prefix)) : 0;
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                  (mask >> 24) & 0xFF, (mask >> 16) & 0xFF,
                  (mask >>  8) & 0xFF,  mask        & 0xFF);
    return buf;
}

// IPv4 in_addr → dotted string
static std::string in4_to_str(const IN_ADDR& a) {
    char buf[20] = {};
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

// ── Interface enumeration ──────────────────────────────────────────────────
static std::vector<ISystem::NetEnvInfo::InterfaceInfo> enum_interfaces() {
    std::vector<ISystem::NetEnvInfo::InterfaceInfo> result;
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS |
                  GAA_FLAG_INCLUDE_WINS_INFO;
    ULONG size = 15000;
    std::vector<uint8_t> buf(size);
    DWORD rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
        reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (rc != ERROR_SUCCESS) return result;

    auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    for (; aa; aa = aa->Next) {
        ISystem::NetEnvInfo::InterfaceInfo iface;

        // Adapter name (GUID string)
        iface.name = aa->AdapterName ? aa->AdapterName : "";
        // Friendly name (wide → UTF-8)
        if (aa->FriendlyName) {
            int n = WideCharToMultiByte(CP_UTF8, 0, aa->FriendlyName, -1,
                                        nullptr, 0, nullptr, nullptr);
            if (n > 0) {
                std::string tmp(n, '\0');
                WideCharToMultiByte(CP_UTF8, 0, aa->FriendlyName, -1,
                                    &tmp[0], n, nullptr, nullptr);
                tmp.resize(strlen(tmp.c_str()));
                iface.friendly_name = tmp;
            }
        }

        iface.is_up       = (aa->OperStatus == IfOperStatusUp);
        iface.is_loopback = (aa->IfType == IF_TYPE_SOFTWARE_LOOPBACK);
        iface.is_wireless = (aa->IfType == IF_TYPE_IEEE80211);
        iface.mtu         = aa->Mtu;

        // TUN/VPN heuristics: adapt type or description
        if (aa->IfType == IF_TYPE_TUNNEL) {
            iface.is_tun = true;
        } else if (aa->IfType == IF_TYPE_PPP) {
            iface.is_vpn = true;
        }
        // Also check description for TAP/TUN keywords
        if (aa->Description) {
            std::wstring desc(aa->Description);
            auto desc_lc = desc;
            for (auto& c : desc_lc) c = towlower(c);
            if (desc_lc.find(L"tap") != std::wstring::npos ||
                desc_lc.find(L"tun")  != std::wstring::npos ||
                desc_lc.find(L"wintun") != std::wstring::npos ||
                desc_lc.find(L"wireguard") != std::wstring::npos)
                iface.is_tun = true;
            if (desc_lc.find(L"vpn")  != std::wstring::npos ||
                desc_lc.find(L"pptp") != std::wstring::npos ||
                desc_lc.find(L"l2tp") != std::wstring::npos)
                iface.is_vpn = true;
        }

        // MAC address
        if (aa->PhysicalAddressLength == 6) {
            char mac[20];
            std::snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                aa->PhysicalAddress[0], aa->PhysicalAddress[1],
                aa->PhysicalAddress[2], aa->PhysicalAddress[3],
                aa->PhysicalAddress[4], aa->PhysicalAddress[5]);
            iface.mac = mac;
        }

        // IP addresses
        for (auto* ua = aa->FirstUnicastAddress; ua; ua = ua->Next) {
            auto* sa = ua->Address.lpSockaddr;
            if (!sa) continue;
            if (sa->sa_family == AF_INET && iface.ipv4.empty()) {
                iface.ipv4    = sockaddr_to_str(sa);
                iface.netmask = prefix_to_netmask(ua->OnLinkPrefixLength);
                // CIDR notation
                iface.ipv4_cidr = iface.ipv4 + "/" +
                                  std::to_string(ua->OnLinkPrefixLength);
                // Broadcast (only valid for IPv4)
                uint32_t ip4, mask4;
                inet_pton(AF_INET, iface.ipv4.c_str(), &ip4);
                inet_pton(AF_INET, iface.netmask.c_str(), &mask4);
                uint32_t bcast = (ip4 & mask4) | ~mask4;
                iface.broadcast = in4_to_str(*reinterpret_cast<IN_ADDR*>(&bcast));
            } else if (sa->sa_family == AF_INET6 && iface.ipv6.empty()) {
                iface.ipv6 = sockaddr_to_str(sa);
            }
        }
        result.push_back(std::move(iface));
    }
    return result;
}

// ── Effective gateway ──────────────────────────────────────────────────────
// Ask the Windows route manager for the route it would actually use to reach
// a public destination.  Enumerating the first 0/0 row is not deterministic:
// VPN, TUN and physical adapters commonly expose several default routes, and
// their route + interface metrics decide which one wins.
static bool fill_best_route(ISystem::NetEnvInfo& info, ADDRESS_FAMILY family) {
    SOCKADDR_INET destination{};
    if (family == AF_INET) {
        destination.Ipv4.sin_family = AF_INET;
        if (inet_pton(AF_INET, "8.8.8.8", &destination.Ipv4.sin_addr) != 1)
            return false;
    } else if (family == AF_INET6) {
        destination.Ipv6.sin6_family = AF_INET6;
        if (inet_pton(AF_INET6, "2001:4860:4860::8888",
                      &destination.Ipv6.sin6_addr) != 1)
            return false;
    } else {
        return false;
    }

    MIB_IPFORWARD_ROW2 route{};
    SOCKADDR_INET source{};
    if (GetBestRoute2(nullptr, 0, nullptr, &destination, 0, &route, &source) !=
        NO_ERROR)
        return false;

    const std::string source_ip = sockaddr_to_str(
        reinterpret_cast<const SOCKADDR*>(&source));
    const std::string gateway = sockaddr_to_str(
        reinterpret_cast<const SOCKADDR*>(&route.NextHop));
    if (family == AF_INET) {
        info.default_gateway_ipv4 = gateway;
        info.egress_ip = source_ip;
    } else {
        info.default_gateway_ipv6 = gateway;
        if (info.egress_ip.empty()) info.egress_ip = source_ip;
    }

    wchar_t alias[IF_MAX_STRING_SIZE + 1] = {};
    if (ConvertInterfaceLuidToAlias(&route.InterfaceLuid, alias,
                                    static_cast<SIZE_T>(std::size(alias))) ==
        NO_ERROR) {
        if (family == AF_INET || info.egress_interface.empty())
            info.egress_interface = wide_to_utf8(alias);
    }

    // Alias conversion can fail for an adapter that is being removed.  The
    // selected source address still lets us map the route to our snapshot.
    if (info.egress_interface.empty() && !source_ip.empty()) {
        for (const auto& iface : info.interfaces) {
            const bool matches = family == AF_INET ? iface.ipv4 == source_ip
                                                    : iface.ipv6 == source_ip;
            if (!matches) continue;
            info.egress_interface = iface.friendly_name.empty()
                                        ? iface.name
                                        : iface.friendly_name;
            break;
        }
    }
    return !source_ip.empty() || !gateway.empty() ||
           route.InterfaceIndex != NET_IFINDEX_UNSPECIFIED;
}

// GetBestRoute is retained only as an IPv4 compatibility fallback.  Unlike a
// raw route-table scan it still applies Windows' metric-aware route selection.
static bool fill_best_ipv4_route_legacy(ISystem::NetEnvInfo& info) {
    IN_ADDR destination{};
    if (inet_pton(AF_INET, "8.8.8.8", &destination) != 1) return false;
    MIB_IPFORWARDROW route{};
    if (GetBestRoute(destination.S_un.S_addr, 0, &route) != NO_ERROR)
        return false;

    info.default_gateway_ipv4 = in4_to_str(
        *reinterpret_cast<const IN_ADDR*>(&route.dwForwardNextHop));
    MIB_IPADDRTABLE* addresses = nullptr;
    ULONG size = 0;
    if (GetIpAddrTable(nullptr, &size, FALSE) == ERROR_INSUFFICIENT_BUFFER)
        addresses = reinterpret_cast<MIB_IPADDRTABLE*>(::malloc(size));
    if (!addresses) return true;
    if (GetIpAddrTable(addresses, &size, FALSE) == NO_ERROR) {
        for (DWORD i = 0; i < addresses->dwNumEntries; ++i) {
            const auto& address = addresses->table[i];
            if (address.dwIndex != route.dwForwardIfIndex) continue;
            info.egress_ip = in4_to_str(
                *reinterpret_cast<const IN_ADDR*>(&address.dwAddr));
            break;
        }
    }
    ::free(addresses);
    for (const auto& iface : info.interfaces) {
        if (iface.ipv4 != info.egress_ip) continue;
        info.egress_interface = iface.friendly_name.empty()
                                    ? iface.name
                                    : iface.friendly_name;
        break;
    }
    return true;
}

static void fill_gateway(ISystem::NetEnvInfo& info) {
    if (!fill_best_route(info, AF_INET))
        fill_best_ipv4_route_legacy(info);
    (void)fill_best_route(info, AF_INET6);
}

// ── DNS servers ────────────────────────────────────────────────────────────
static void fill_dns(ISystem::NetEnvInfo& info) {
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
    ULONG size  = 15000;
    std::vector<uint8_t> buf(size);
    if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size)
        == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    for (; aa; aa = aa->Next) {
        if (aa->OperStatus != IfOperStatusUp) continue;
        for (auto* dns = aa->FirstDnsServerAddress; dns; dns = dns->Next) {
            auto s = sockaddr_to_str(dns->Address.lpSockaddr);
            if (!s.empty()) {
                bool dup = false;
                for (auto& d : info.dns_servers) if (d == s) { dup = true; break; }
                if (!dup) info.dns_servers.push_back(s);
            }
        }
    }
}

// ── System proxy ───────────────────────────────────────────────────────────
static void fill_proxy(ISystem::NetEnvInfo& info) {
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie{};
    if (!WinHttpGetIEProxyConfigForCurrentUser(&ie)) {
        info.proxy.detection_known = false;
        return;
    }
    info.proxy.detection_known = true;

    // WINHTTP_CURRENT_USER_IE_PROXY_CONFIG has no ProxyEnable member and may
    // retain lpszProxy after an application such as Clash disables the Windows
    // manual proxy. Respect the authoritative Internet Settings switch so a
    // stale loopback endpoint is not treated as a live bridge hop.
    constexpr const char* kInternetSettings =
        "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";
    constexpr DWORD kProxyEnableUnknown = 0xFFFFFFFFu;
    const DWORD proxy_enable = reg_dword(
        HKEY_CURRENT_USER, kInternetSettings, "ProxyEnable",
        kProxyEnableUnknown);
    const bool manual_proxy_enabled =
        proxy_enable == kProxyEnableUnknown ? ie.lpszProxy != nullptr
                                            : proxy_enable != 0;

    if (ie.fAutoDetect) {
        info.proxy.auto_detect = true;
    }
    if (ie.lpszAutoConfigUrl && ie.lpszAutoConfigUrl[0]) {
        info.proxy.type = ISystem::NetEnvInfo::ProxyInfo::Type::PAC;
        info.proxy.type_str = "PAC";
        // Convert wide → UTF-8
        int n = WideCharToMultiByte(CP_UTF8, 0, ie.lpszAutoConfigUrl, -1,
                                    nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            std::string s(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, ie.lpszAutoConfigUrl, -1,
                                &s[0], n, nullptr, nullptr);
            s.resize(strlen(s.c_str()));
            info.proxy.pac_url = s;
        }
    } else if (manual_proxy_enabled && ie.lpszProxy && ie.lpszProxy[0]) {
        // Proxy string may be "host:port" or "http=host:port;https=host:port"
        int n = WideCharToMultiByte(CP_UTF8, 0, ie.lpszProxy, -1,
                                    nullptr, 0, nullptr, nullptr);
        std::string proxy_str;
        if (n > 0) {
            proxy_str.resize(n);
            WideCharToMultiByte(CP_UTF8, 0, ie.lpszProxy, -1,
                                &proxy_str[0], n, nullptr, nullptr);
            proxy_str.resize(strlen(proxy_str.c_str()));
        }

        // Parse the first entry.  Common formats:
        // "192.168.1.1:1080"
        // "socks=192.168.1.1:1080"
        // "http=proxy.corp:8080;https=proxy.corp:8080"
        std::string entry = proxy_str;
        auto semi = entry.find(';');
        if (semi != std::string::npos) entry = entry.substr(0, semi);

        std::string scheme, hostport;
        auto eq = entry.find('=');
        if (eq != std::string::npos) {
            scheme   = entry.substr(0, eq);
            hostport = entry.substr(eq + 1);
        } else {
            hostport = entry;
        }

        // Determine proxy type
        if (scheme == "socks" || scheme == "socks5") {
            info.proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::SOCKS5;
            info.proxy.type_str = "SOCKS5";
        } else if (scheme == "socks4") {
            info.proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::SOCKS4;
            info.proxy.type_str = "SOCKS4";
        } else if (scheme == "https") {
            info.proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::HTTPS;
            info.proxy.type_str = "HTTPS";
        } else {
            info.proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::HTTP;
            info.proxy.type_str = "HTTP";
        }

        // Split host:port
        auto colon = hostport.rfind(':');
        if (colon != std::string::npos) {
            info.proxy.host = hostport.substr(0, colon);
            info.proxy.port = static_cast<uint16_t>(
                std::stoul(hostport.substr(colon + 1)));
        } else {
            info.proxy.host = hostport;
        }

        // Bypass list
        if (ie.lpszProxyBypass && ie.lpszProxyBypass[0]) {
            int m = WideCharToMultiByte(CP_UTF8, 0, ie.lpszProxyBypass, -1,
                                        nullptr, 0, nullptr, nullptr);
            if (m > 0) {
                std::string byp(m, '\0');
                WideCharToMultiByte(CP_UTF8, 0, ie.lpszProxyBypass, -1,
                                    &byp[0], m, nullptr, nullptr);
                byp.resize(strlen(byp.c_str()));
                // Split on ';' or ' '
                std::istringstream ss(byp);
                std::string tok;
                while (std::getline(ss, tok, ';'))
                    if (!tok.empty()) info.proxy.bypass_list.push_back(tok);
            }
        }
    } else {
        info.proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::None;
        info.proxy.type_str = "None";
    }

    if (ie.lpszProxy)         GlobalFree(ie.lpszProxy);
    if (ie.lpszProxyBypass)   GlobalFree(ie.lpszProxyBypass);
    if (ie.lpszAutoConfigUrl) GlobalFree(ie.lpszAutoConfigUrl);
}

// ── TCP stack parameters ───────────────────────────────────────────────────
static void fill_tcp_stack(ISystem::NetEnvInfo& info) {
    const char* tcp_key =
        "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters";
    auto& ts = info.tcp_stack;

    // Default TTL
    DWORD ttl = reg_dword(HKEY_LOCAL_MACHINE, tcp_key, "DefaultTTL", 128);
    ts.default_ttl = ttl;

    // TCP window auto-tuning (TcpAutoTuningLevel)
    // Normal = 0 (scaling enabled), Disabled = 4
    DWORD autotuning = reg_dword(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
        "TcpAutoTuningLevel", 0); // 0=normal (scaling on)
    ts.window_scaling = (autotuning != 4);

    // Global max TCP buffer (TcpWindowSize registry = max receive window bytes)
    DWORD win = reg_dword(HKEY_LOCAL_MACHINE, tcp_key, "TcpWindowSize", 0);
    if (win == 0) win = 64 * 1024; // Windows default initial window (64 KB)
    ts.recv_window_size = win;

    // Windows auto-tunes to a default initial window that aligns with
    // 65536 * 2^N (auto-tuning level). For reporting we use a known default.
    ts.max_window_size = 16 * 1024 * 1024; // 16 MB (typical modern Windows max)
    ts.window_scale    = 8;                 // 2^8 = 256
    ts.send_buffer_size = 64 * 1024;        // typical SO_SNDBUF default

    // Report the standard Ethernet MSS; retransmission count is unrelated.
    ts.mss = 1460; // standard Ethernet MSS

    // SACK
    DWORD sack = reg_dword(HKEY_LOCAL_MACHINE, tcp_key, "SackOpts", 1);
    ts.sack_enabled = (sack != 0);

    // Timestamps
    DWORD ts_en = reg_dword(HKEY_LOCAL_MACHINE, tcp_key, "Tcp1323Opts", 0);
    ts.timestamps     = (ts_en & 2) != 0;

    // ECN
    DWORD ecn = reg_dword(HKEY_LOCAL_MACHINE, tcp_key, "TCPECNCapabilityEnabled", 0);
    ts.ecn_enabled = (ecn != 0);

    // Keepalive
    DWORD ka = reg_dword(HKEY_LOCAL_MACHINE, tcp_key, "KeepAliveTime", 7200000);
    ts.keepalive_time_sec  = ka / 1000;
    DWORD ki = reg_dword(HKEY_LOCAL_MACHINE, tcp_key, "KeepAliveInterval", 1000);
    ts.keepalive_intvl_sec = ki / 1000;
    ts.keepalive_probes    = 10; // Windows default
}

// ── TLS fingerprint (SChannel) ──────────────────────────────────────────────
static void fill_tls_fp(ISystem::NetEnvInfo& info) {
    auto& fp = info.tls_fp;

    // Determine max TLS version from registry and OS version
    // Windows 10 1903+ supports TLS 1.3 in SChannel (build >= 18362)
    OSVERSIONINFOEXW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    typedef LONG(WINAPI* RtlGetVersion_t)(OSVERSIONINFOEXW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    bool have_tls13 = false;
    if (ntdll) {
        auto fn = reinterpret_cast<RtlGetVersion_t>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (fn && fn(&vi) == 0)
            have_tls13 = (vi.dwBuildNumber >= 18362);
    }

    fp.tls_version_max = have_tls13 ? 0x0304 : 0x0303;
    fp.tls_version_str = tls_version_str(fp.tls_version_max);

    // Query SChannel cipher suite configuration from registry.
    // Key: HKLM\SYSTEM\CurrentControlSet\Control\Cryptography\Configuration\
    //       Local\SSL\00010003  Value: Functions (REG_MULTI_SZ)
    auto names = reg_multi_sz(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Cryptography\\Configuration"
        "\\Local\\SSL\\00010003",
        "Functions");

    if (!names.empty()) {
        for (auto& n : names) {
            uint16_t code = cipher_name_to_iana(n);
            if (code != 0) fp.cipher_suites.push_back(code);
        }
    }

    // If registry was empty / unavailable, fall back to Windows 11 defaults
    if (fp.cipher_suites.empty()) {
        if (have_tls13) {
            fp.cipher_suites = {
                0x1302, // TLS_AES_256_GCM_SHA384
                0x1301, // TLS_AES_128_GCM_SHA256
                0x1303, // TLS_CHACHA20_POLY1305_SHA256
            };
        }
        fp.cipher_suites.insert(fp.cipher_suites.end(), {
            0xC02C, 0xC030, 0xC02B, 0xC02F, // ECDHE-ECDSA/RSA GCM
            0xCCA9, 0xCCA8,                  // ChaCha20 ECDHE
            0xC024, 0xC028, 0xC023, 0xC027,  // ECDHE CBC SHA384/256
            0xC00A, 0xC014, 0xC009, 0xC013,  // ECDHE CBC SHA
            0x009D, 0x009C,                  // RSA GCM
            0x003D, 0x003C, 0x0035, 0x002F,  // RSA CBC
        });
    }

    // Extensions – standard SChannel / Windows TLS order
    fp.extensions = {
        0,      // server_name (SNI)
        5,      // status_request
        10,     // supported_groups
        11,     // ec_point_formats
        13,     // signature_algorithms
        16,     // ALPN
        18,     // signed_certificate_timestamp
        21,     // padding
        23,     // extended_master_secret
        27,     // compress_certificate
        35,     // session_ticket
        43,     // supported_versions
        45,     // psk_key_exchange_modes
        51,     // key_share
        65281,  // renegotiation_info
    };

    // Elliptic curves / supported_groups (Windows preference order)
    fp.groups = {
        29,  // x25519
        23,  // secp256r1
        24,  // secp384r1
        25,  // secp521r1
        256, // ffdhe2048
        257, // ffdhe3072
    };

    // EC point formats: uncompressed only (SChannel)
    fp.ec_point_fmts = { 0 };

    // ALPN: typical Windows browser-style
    fp.alpn = { "h2", "http/1.1" };

    compute_tls_fingerprints(fp);
}

// ── Locale / timezone / language ──────────────────────────────────────────
static void fill_locale(ISystem::NetEnvInfo& info) {
    auto& lo = info.locale;

    // Timezone
    TIME_ZONE_INFORMATION tz{};
    DWORD tzr = GetTimeZoneInformation(&tz);

    // Offset in seconds (Bias is in minutes, west negative)
    int bias_min = -(int)tz.Bias;
    if (tzr == TIME_ZONE_ID_DAYLIGHT) bias_min -= (int)tz.DaylightBias;
    else if (tzr == TIME_ZONE_ID_STANDARD) bias_min -= (int)tz.StandardBias;
    lo.utc_offset_sec = bias_min * 60;

    char off_buf[16];
    int  abs_min = (bias_min < 0) ? -bias_min : bias_min;
    std::snprintf(off_buf, sizeof(off_buf), "%c%02d:%02d",
                  bias_min < 0 ? '-' : '+',
                  abs_min / 60, abs_min % 60);
    lo.utc_offset_str = off_buf;

    // Timezone abbreviation from the active name field
    {
        const wchar_t* wname = (tzr == TIME_ZONE_ID_DAYLIGHT)
                               ? tz.DaylightName : tz.StandardName;
        int n = WideCharToMultiByte(CP_UTF8, 0, wname, -1, nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            std::string s(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wname, -1, &s[0], n, nullptr, nullptr);
            s.resize(strlen(s.c_str()));
            lo.timezone_abbr = s;
        }
    }

    // timezone_name: try to read from registry (TZI MUI display)
    {
        // HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Time Zones\<TzName>
        // Use GetDynamicTimeZoneInformation to get the key name
        DYNAMIC_TIME_ZONE_INFORMATION dtz{};
        if (GetDynamicTimeZoneInformation(&dtz) != TIME_ZONE_ID_INVALID) {
            char key_name[512];
            WideCharToMultiByte(CP_UTF8, 0, dtz.TimeZoneKeyName, -1,
                                key_name, sizeof(key_name), nullptr, nullptr);
            lo.timezone_name = key_name;
        }
    }

    // Language and locale via GetLocaleInfoEx
    {
        wchar_t lang[LOCALE_NAME_MAX_LENGTH] = {};
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SNAME, lang,
                        LOCALE_NAME_MAX_LENGTH);
        int n = WideCharToMultiByte(CP_UTF8, 0, lang, -1, nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            std::string s(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, lang, -1, &s[0], n, nullptr, nullptr);
            s.resize(strlen(s.c_str()));
            // "zh-CN" already IETF BCP-47 format
            lo.language = s;
            // Build locale string
            lo.locale = s + ".UTF-8";
        }

        // Country code
        wchar_t country[10] = {};
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SISO3166CTRYNAME,
                        country, 10);
        n = WideCharToMultiByte(CP_UTF8, 0, country, -1, nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            std::string s(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, country, -1, &s[0], n, nullptr, nullptr);
            s.resize(strlen(s.c_str()));
            lo.country_code = s;
        }
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ISystem::GetNetEnvironment  –  Windows implementation
// ─────────────────────────────────────────────────────────────────────────────
ISystem::NetEnvInfo ISystem::GetNetEnvironment() {
    NetEnvInfo info;

    // 1. Interfaces
    info.interfaces = enum_interfaces();

    // 2. Gateway + egress IP
    fill_gateway(info);

    // 3. DNS
    fill_dns(info);

    // 4. Proxy
    fill_proxy(info);

    // 5. TUN / VPN detection
    for (auto& iface : info.interfaces) {
        if (iface.is_tun && iface.is_up && !iface.ipv4.empty())
            info.tun_vpn.tun_ifaces.push_back(
                iface.friendly_name.empty() ? iface.name : iface.friendly_name);
        if (iface.is_vpn && iface.is_up && !iface.ipv4.empty())
            info.tun_vpn.vpn_ifaces.push_back(
                iface.friendly_name.empty() ? iface.name : iface.friendly_name);
    }
    info.tun_vpn.tun_active = !info.tun_vpn.tun_ifaces.empty();
    info.tun_vpn.vpn_active = !info.tun_vpn.vpn_ifaces.empty();

    // 6. TCP stack
    fill_tcp_stack(info);

    // 7. TLS fingerprint
    fill_tls_fp(info);

    // 8. Locale
    fill_locale(info);

    // 9. Connection type classification
    if (info.tun_vpn.tun_active) {
        info.connection_type     = NetEnvInfo::ConnType::TUN;
        info.connection_type_str = "TUN";
    } else if (info.tun_vpn.vpn_active) {
        info.connection_type     = NetEnvInfo::ConnType::VPN;
        info.connection_type_str = "VPN";
    } else if (info.proxy.type != NetEnvInfo::ProxyInfo::Type::None) {
        info.connection_type     = NetEnvInfo::ConnType::Proxy;
        info.connection_type_str = "Proxy";
    } else {
        info.connection_type     = NetEnvInfo::ConnType::Direct;
        info.connection_type_str = "Direct";
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────

bool ISystem::is_tcp_port_available(const unsigned short& port,
	const std::string& address, std::string* out_err) {
#if defined(__OSWIN__)
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		if (out_err) *out_err = "WSAStartup failed";
		return false;
	}
#endif

	bool available = false;
	struct addrinfo hints;
	struct addrinfo* res = nullptr;
	char portstr[8];
	std::snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);

	std::memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	// allow both IPv4 and IPv6 results for the given address
	hints.ai_family = AF_UNSPEC;

	int gai_ret = getaddrinfo(address.c_str(), portstr, &hints, &res);
	if (gai_ret != 0) {
#if defined(__OSWIN__)
		if (out_err) *out_err = std::string("getaddrinfo failed: ") + std::to_string(gai_ret);
		WSACleanup();
#else
		if (out_err) *out_err = std::string("getaddrinfo failed: ") + gai_strerror(gai_ret);
#endif
		return false;
	}

	int last_sock_err = 0;
	for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
#ifdef _WIN32
		SOCKET s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (s == INVALID_SOCKET) {
			last_sock_err = WSAGetLastError();
			continue;
		}
		// allow immediate reuse for testing
		int opt = 1;
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

		if (bind(s, p->ai_addr, (int)p->ai_addrlen) == SOCKET_ERROR) {
			last_sock_err = WSAGetLastError();
			closesocket(s);
			continue;
		}
		if (listen(s, 1) == SOCKET_ERROR) {
			last_sock_err = WSAGetLastError();
			closesocket(s);
			continue;
		}
		// success
		closesocket(s);
		available = true;
		break;
#else
		int s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (s < 0) {
			last_sock_err = errno;
			continue;
		}
		int opt = 1;
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
		if (bind(s, p->ai_addr, p->ai_addrlen) != 0) {
			last_sock_err = errno;
			close(s);
			continue;
		}
		if (listen(s, 1) != 0) {
			last_sock_err = errno;
			close(s);
			continue;
		}
		// success
		close(s);
		available = true;
		break;
#endif
	}

	freeaddrinfo(res);

	if (!available) {
		if (out_err) {
#if defined(__OSWIN__)
			*out_err = std::string("socket error: ") + std::to_string(last_sock_err);
			WSACleanup();
#else
			* out_err = std::string("socket error: ") + std::strerror(last_sock_err);
#endif
		}
		return false;
	}

#if defined(__OSWIN__)
	WSACleanup();
#endif
	return true;
}
