#include <libsys.h>
#include "../net_env_impl.hpp"

#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <langinfo.h>
#include <locale.h>
#include <time.h>
#include <map>

#include <SystemConfiguration/SystemConfiguration.h>
// SecureTransport is deprecated in macOS 12 but still functional;
// suppress deprecation warnings for cipher-suite enumeration.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <Security/SecureTransport.h>
#pragma clang diagnostic pop

// ─────────────────────────────────────────────────────────────────────────────
namespace {

// sockaddr → string
static std::string sa_to_str(const sockaddr* sa) {
    char buf[128] = {};
    if (!sa) return {};
    if (sa->sa_family == AF_INET)
        inet_ntop(AF_INET,  &reinterpret_cast<const sockaddr_in*>(sa)->sin_addr,  buf, sizeof(buf));
    else if (sa->sa_family == AF_INET6)
        inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr, buf, sizeof(buf));
    return buf;
}

// Broadcast from IPv4 address + netmask
static std::string calc_broadcast(const std::string& ip, const std::string& mask) {
    in_addr ia{}, im{};
    if (inet_pton(AF_INET, ip.c_str(), &ia) != 1 ||
        inet_pton(AF_INET, mask.c_str(), &im) != 1) return {};
    in_addr ib;
    ib.s_addr = (ia.s_addr & im.s_addr) | ~im.s_addr;
    char buf[20] = {};
    inet_ntop(AF_INET, &ib, buf, sizeof(buf));
    return buf;
}

// Read a sysctl integer
static long sysctl_int(const char* name, long def = 0) {
    int v = 0; size_t sz = sizeof(v);
    if (sysctlbyname(name, &v, &sz, nullptr, 0) == 0) return v;
    return def;
}

// ── Interfaces ─────────────────────────────────────────────────────────────
static void fill_interfaces(ISystem::NetEnvInfo& info) {
    struct ifaddrs* ifa_head = nullptr;
    if (getifaddrs(&ifa_head) != 0) return;

    // First pass: collect per-interface structural info from AF_LINK entries
    // Second pass: attach IPv4/IPv6 from AF_INET entries
    // We store data in a map (name → InterfaceInfo)
    std::map<std::string, ISystem::NetEnvInfo::InterfaceInfo> imap;

    for (auto* ifa = ifa_head; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name) continue;
        std::string name = ifa->ifa_name;
        auto& iface = imap[name];
        iface.name = name;
        iface.is_up       = (ifa->ifa_flags & IFF_UP) != 0;
        iface.is_loopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;

        // TUN/VPN heuristics by name
        if (name.rfind("utun", 0) == 0 || name.rfind("tun", 0) == 0 ||
            name.rfind("tap", 0) == 0)
            iface.is_tun = true;
        if (name.rfind("ppp", 0) == 0 || name.rfind("ipsec", 0) == 0 ||
            name.rfind("ipsec", 0) == 0)
            iface.is_vpn = true;

        // Wireless: en* interfaces can be Wi-Fi but we can't easily distinguish
        // without IOKit; skip for now.

        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_LINK) {
            auto* sdl = reinterpret_cast<sockaddr_dl*>(ifa->ifa_addr);
            iface.mtu = 0; // filled by ioctl or defaults
            if (sdl->sdl_alen == 6) {
                const auto* m = reinterpret_cast<const uint8_t*>(LLADDR(sdl));
                char mac[18];
                std::snprintf(mac, sizeof(mac),
                    "%02x:%02x:%02x:%02x:%02x:%02x",
                    m[0], m[1], m[2], m[3], m[4], m[5]);
                iface.mac = mac;
            }
        } else if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
            if (iface.ipv4.empty()) {
                iface.ipv4 = sa_to_str(ifa->ifa_addr);
                if (ifa->ifa_netmask) {
                    iface.netmask = sa_to_str(ifa->ifa_netmask);
                    // Compute prefix length
                    in_addr mask{};
                    inet_pton(AF_INET, iface.netmask.c_str(), &mask);
                    uint32_t m = ntohl(mask.s_addr);
                    int prefix = 0;
                    while (m & 0x80000000u) { prefix++; m <<= 1; }
                    iface.ipv4_cidr = iface.ipv4 + "/" + std::to_string(prefix);
                }
                if (ifa->ifa_broadaddr)
                    iface.broadcast = sa_to_str(ifa->ifa_broadaddr);
                else if (!iface.netmask.empty())
                    iface.broadcast = calc_broadcast(iface.ipv4, iface.netmask);
            }
        } else if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET6) {
            if (iface.ipv6.empty())
                iface.ipv6 = sa_to_str(ifa->ifa_addr);
        }
    }
    freeifaddrs(ifa_head);

    // Get MTU via sysctl
    for (auto& kv : imap) {
        int mib[6] = { CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST, 0 };
        (void)mib; // MTU via sockaddr_dl ifm_data is complex; default to 1500
        kv.second.mtu = 1500;
    }

    for (auto& kv : imap)
        info.interfaces.push_back(kv.second);
}

// ── Default gateway (parse sysctl routing table) ──────────────────────────
static void fill_gateway(ISystem::NetEnvInfo& info) {
    // Use sysctl(CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_DUMP, 0) to get routes
    int mib[] = { CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_DUMP, 0 };
    size_t needed = 0;
    if (sysctl(mib, 6, nullptr, &needed, nullptr, 0) < 0 || needed == 0)
        goto fallback;

    {
        std::vector<char> buf(needed);
        if (sysctl(mib, 6, buf.data(), &needed, nullptr, 0) < 0)
            goto fallback;

        char* p = buf.data();
        char* end = buf.data() + needed;
        while (p < end) {
            auto* rtm = reinterpret_cast<rt_msghdr*>(p);
            if (rtm->rtm_msglen == 0) break;
            if (rtm->rtm_type == RTM_GET || rtm->rtm_flags & RTF_UP) {
                // Check if this is a default route (dest == 0.0.0.0)
                auto* sa = reinterpret_cast<sockaddr*>(rtm + 1);
                if (rtm->rtm_addrs & RTA_DST) {
                    if (sa->sa_family == AF_INET) {
                        auto* sin = reinterpret_cast<sockaddr_in*>(sa);
                        if (sin->sin_addr.s_addr == 0) {
                            // default route – next is gateway
                            // RTA_GATEWAY follows RTA_DST
                            char* q = reinterpret_cast<char*>(sa);
                            q += (sa->sa_len + 3) & ~3; // align
                            if (rtm->rtm_addrs & RTA_GATEWAY) {
                                auto* gw = reinterpret_cast<sockaddr*>(q);
                                if (gw->sa_family == AF_INET && info.default_gateway_ipv4.empty()) {
                                    char gbuf[20] = {};
                                    inet_ntop(AF_INET,
                                        &reinterpret_cast<sockaddr_in*>(gw)->sin_addr,
                                        gbuf, sizeof(gbuf));
                                    info.default_gateway_ipv4 = gbuf;
                                }
                            }
                        }
                    }
                }
            }
            p += rtm->rtm_msglen;
        }
    }

fallback:
    // Set egress_ip: first UP non-loopback non-TUN interface with IPv4
    for (auto& iface : info.interfaces) {
        if (!iface.is_loopback && iface.is_up && !iface.ipv4.empty() && !iface.is_tun) {
            info.egress_interface = iface.name;
            info.egress_ip        = iface.ipv4;
            break;
        }
    }
}

// ── DNS from /etc/resolv.conf and SystemConfiguration ─────────────────────
static void fill_dns(ISystem::NetEnvInfo& info) {
    // Primary: use SystemConfiguration dynamic store
    SCDynamicStoreRef store = SCDynamicStoreCreate(nullptr, CFSTR("libsys"), nullptr, nullptr);
    if (store) {
        CFStringRef key = SCDynamicStoreKeyCreateNetworkGlobalEntity(
            nullptr, kSCDynamicStoreDomainState, kSCEntNetDNS);
        if (key) {
            CFDictionaryRef dns_dict = reinterpret_cast<CFDictionaryRef>(
                SCDynamicStoreCopyValue(store, key));
            if (dns_dict) {
                CFArrayRef servers = reinterpret_cast<CFArrayRef>(
                    CFDictionaryGetValue(dns_dict, CFSTR("ServerAddresses")));
                if (servers) {
                    CFIndex n = CFArrayGetCount(servers);
                    for (CFIndex i = 0; i < n; i++) {
                        auto* s = reinterpret_cast<CFStringRef>(CFArrayGetValueAtIndex(servers, i));
                        char buf[64] = {};
                        if (CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8))
                            info.dns_servers.emplace_back(buf);
                    }
                }
                CFRelease(dns_dict);
            }
            CFRelease(key);
        }
        CFRelease(store);
    }

    // Fallback: /etc/resolv.conf
    if (info.dns_servers.empty()) {
        FILE* f = fopen("/etc/resolv.conf", "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "nameserver", 10) == 0) {
                    char ns[64] = {};
                    if (sscanf(line + 10, " %63s", ns) == 1)
                        info.dns_servers.emplace_back(ns);
                }
            }
            fclose(f);
        }
    }
}

// ── System proxy via SystemConfiguration ──────────────────────────────────
static void fill_proxy(ISystem::NetEnvInfo& info) {
    CFDictionaryRef proxy_dict = SCDynamicStoreCopyProxies(nullptr);
    if (!proxy_dict) return;

    auto get_bool = [&](CFStringRef key) -> bool {
        CFNumberRef n = reinterpret_cast<CFNumberRef>(
            CFDictionaryGetValue(proxy_dict, key));
        if (!n) return false;
        int v = 0; CFNumberGetValue(n, kCFNumberIntType, &v);
        return v != 0;
    };
    auto get_int = [&](CFStringRef key) -> int {
        CFNumberRef n = reinterpret_cast<CFNumberRef>(
            CFDictionaryGetValue(proxy_dict, key));
        if (!n) return 0;
        int v = 0; CFNumberGetValue(n, kCFNumberIntType, &v);
        return v;
    };
    auto get_str = [&](CFStringRef key) -> std::string {
        CFStringRef s = reinterpret_cast<CFStringRef>(
            CFDictionaryGetValue(proxy_dict, key));
        if (!s) return {};
        char buf[512] = {};
        CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8);
        return buf;
    };

    info.proxy.auto_detect = get_bool(kSCPropNetProxiesProxyAutoDiscoveryEnable);

    // PAC URL
    if (get_bool(kSCPropNetProxiesProxyAutoConfigEnable)) {
        info.proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::PAC;
        info.proxy.type_str = "PAC";
        info.proxy.pac_url  = get_str(kSCPropNetProxiesProxyAutoConfigURLString);
    }
    // SOCKS proxy takes precedence in most tools
    else if (get_bool(kSCPropNetProxiesSOCKSEnable)) {
        info.proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::SOCKS5;
        info.proxy.type_str = "SOCKS5";
        info.proxy.host     = get_str(kSCPropNetProxiesSOCKSProxy);
        info.proxy.port     = static_cast<uint16_t>(get_int(kSCPropNetProxiesSOCKSPort));
    } else if (get_bool(kSCPropNetProxiesHTTPSEnable)) {
        info.proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::HTTPS;
        info.proxy.type_str = "HTTPS";
        info.proxy.host     = get_str(kSCPropNetProxiesHTTPSProxy);
        info.proxy.port     = static_cast<uint16_t>(get_int(kSCPropNetProxiesHTTPSPort));
    } else if (get_bool(kSCPropNetProxiesHTTPEnable)) {
        info.proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::HTTP;
        info.proxy.type_str = "HTTP";
        info.proxy.host     = get_str(kSCPropNetProxiesHTTPProxy);
        info.proxy.port     = static_cast<uint16_t>(get_int(kSCPropNetProxiesHTTPPort));
    } else {
        info.proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::None;
        info.proxy.type_str = "None";
    }

    // Bypass list
    CFArrayRef exc = reinterpret_cast<CFArrayRef>(
        CFDictionaryGetValue(proxy_dict, kSCPropNetProxiesExceptionsList));
    if (exc) {
        CFIndex n = CFArrayGetCount(exc);
        for (CFIndex i = 0; i < n; i++) {
            auto* s = reinterpret_cast<CFStringRef>(CFArrayGetValueAtIndex(exc, i));
            char buf[256] = {};
            if (CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8))
                info.proxy.bypass_list.emplace_back(buf);
        }
    }

    CFRelease(proxy_dict);
}

// ── TCP stack ──────────────────────────────────────────────────────────────
static void fill_tcp_stack(ISystem::NetEnvInfo& info) {
    auto& ts = info.tcp_stack;

    ts.default_ttl        = static_cast<uint32_t>(sysctl_int("net.inet.ip.ttl", 64));
    ts.recv_window_size   = static_cast<uint32_t>(sysctl_int("net.inet.tcp.recvspace", 131072));
    ts.send_buffer_size   = static_cast<uint32_t>(sysctl_int("net.inet.tcp.sendspace", 131072));
    ts.window_scaling     = sysctl_int("net.inet.tcp.rfc1323", 1) != 0;
    ts.sack_enabled       = sysctl_int("net.inet.tcp.sack", 1) != 0;
    ts.timestamps         = sysctl_int("net.inet.tcp.rfc1323", 1) != 0;
    ts.ecn_enabled        = sysctl_int("net.inet.tcp.ecn_initiate_out", 0) != 0;
    ts.keepalive_time_sec = static_cast<uint32_t>(sysctl_int("net.inet.tcp.keepidle", 7200) / 1000);
    ts.keepalive_intvl_sec= static_cast<uint32_t>(sysctl_int("net.inet.tcp.keepintvl", 75) / 1000);
    ts.keepalive_probes   = static_cast<uint8_t>(sysctl_int("net.inet.tcp.keepcnt", 8));
    ts.mss                = 1460;
    ts.window_scale       = 6; // log2(recv_window / 65535)
    ts.max_window_size    = static_cast<uint32_t>(
        sysctl_int("net.inet.tcp.autorcvbufmax", 4 * 1024 * 1024));
}

// ── TLS fingerprint (SecureTransport) ─────────────────────────────────────
static void fill_tls_fp(ISystem::NetEnvInfo& info) {
    auto& fp = info.tls_fp;

    // macOS 10.15+ supports TLS 1.3 via SecureTransport/Network.framework
    // Detect via deployment target or OS version.
    int major = 0;
    char ver[64] = {};
    size_t sz = sizeof(ver);
    sysctlbyname("kern.osproductversion", ver, &sz, nullptr, 0);
    if (ver[0]) major = std::atoi(ver);
    bool have_tls13 = (major >= 14); // conservative: Sonoma+

    fp.tls_version_max = have_tls13 ? 0x0304 : 0x0303;
    fp.tls_version_str = tls_version_str(fp.tls_version_max);

    // Enumerate supported cipher suites via SecureTransport (deprecated but available)
    {
        SSLContextRef ctx = SSLCreateContext(nullptr, kSSLClientSide, kSSLStreamType);
        if (ctx) {
            size_t num_supported = 0;
            if (SSLGetNumberSupportedCiphers(ctx, &num_supported) == errSecSuccess
                && num_supported > 0) {
                std::vector<SSLCipherSuite> suites(num_supported);
                if (SSLGetSupportedCiphers(ctx, suites.data(), &num_supported) == errSecSuccess) {
                    for (size_t i = 0; i < num_supported; i++) {
                        uint16_t code = static_cast<uint16_t>(suites[i]);
                        // Skip GREASE and null ciphers
                        if (!nenv_detail::is_grease(code) && code != 0x0000)
                            fp.cipher_suites.push_back(code);
                    }
                }
            }
            CFRelease(ctx);
        }
    }

    // Add TLS 1.3 ciphers at the front (if supported)
    if (have_tls13) {
        std::vector<uint16_t> tls13 = { 0x1301, 0x1302, 0x1303 };
        // prepend
        fp.cipher_suites.insert(fp.cipher_suites.begin(), tls13.begin(), tls13.end());
    }

    // Fallback if enumeration failed
    if (fp.cipher_suites.empty()) {
        if (have_tls13)
            fp.cipher_suites = { 0x1301, 0x1302, 0x1303 };
        fp.cipher_suites.insert(fp.cipher_suites.end(), {
            0xC02C, 0xC02B, 0xCCA9, 0xC030, 0xC02F, 0xCCA8,
            0xC024, 0xC023, 0xC028, 0xC027,
            0xC00A, 0xC009, 0xC014, 0xC013,
            0x009D, 0x009C, 0x003D, 0x003C, 0x0035, 0x002F,
        });
    }

    // Standard macOS TLS extension order
    fp.extensions = {
        0,     // server_name
        5,     // status_request
        10,    // supported_groups
        11,    // ec_point_formats
        13,    // signature_algorithms
        16,    // ALPN
        18,    // signed_certificate_timestamp
        21,    // padding
        23,    // extended_master_secret
        35,    // session_ticket
        43,    // supported_versions
        45,    // psk_key_exchange_modes
        51,    // key_share
        65281, // renegotiation_info
    };

    fp.groups = {
        29,  // x25519
        23,  // secp256r1
        24,  // secp384r1
        25,  // secp521r1
    };
    fp.ec_point_fmts = { 0 };
    fp.alpn = { "h2", "http/1.1" };

    compute_tls_fingerprints(fp);
}

// ── Locale / timezone ──────────────────────────────────────────────────────
static void fill_locale(ISystem::NetEnvInfo& info) {
    auto& lo = info.locale;

    // Timezone via CoreFoundation
    CFTimeZoneRef tz = CFTimeZoneCopyDefault();
    if (tz) {
        // timezone_name
        CFStringRef tzname = CFTimeZoneGetName(tz);
        if (tzname) {
            char buf[128] = {};
            if (CFStringGetCString(tzname, buf, sizeof(buf), kCFStringEncodingUTF8))
                lo.timezone_name = buf;
        }

        // UTC offset (seconds) at current time
        CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
        CFTimeInterval offset = CFTimeZoneGetSecondsFromGMT(tz, now);
        lo.utc_offset_sec = static_cast<int>(offset);

        // Format "+08:00"
        int abs_sec = lo.utc_offset_sec < 0 ? -lo.utc_offset_sec : lo.utc_offset_sec;
        char offbuf[16];
        std::snprintf(offbuf, sizeof(offbuf), "%c%02d:%02d",
            lo.utc_offset_sec < 0 ? '-' : '+',
            abs_sec / 3600, (abs_sec % 3600) / 60);
        lo.utc_offset_str = offbuf;

        // Abbreviation (e.g. "CST")
        CFStringRef abbr = CFTimeZoneCopyAbbreviation(tz, now);
        if (abbr) {
            char buf[64] = {};
            if (CFStringGetCString(abbr, buf, sizeof(buf), kCFStringEncodingUTF8))
                lo.timezone_abbr = buf;
            CFRelease(abbr);
        }
        CFRelease(tz);
    }

    // Language and locale via CFLocale
    CFLocaleRef locale = CFLocaleCopyCurrent();
    if (locale) {
        // Language
        CFStringRef lang = reinterpret_cast<CFStringRef>(
            CFLocaleGetValue(locale, kCFLocaleLanguageCode));
        CFStringRef country = reinterpret_cast<CFStringRef>(
            CFLocaleGetValue(locale, kCFLocaleCountryCode));

        char lang_buf[32] = {}, country_buf[10] = {};
        if (lang)    CFStringGetCString(lang,    lang_buf,    sizeof(lang_buf),    kCFStringEncodingUTF8);
        if (country) CFStringGetCString(country, country_buf, sizeof(country_buf), kCFStringEncodingUTF8);

        lo.country_code = country_buf;
        // Build BCP-47 language tag
        if (country_buf[0])
            lo.language = std::string(lang_buf) + "-" + country_buf;
        else
            lo.language = lang_buf;

        CFRelease(locale);
    }

    // Locale name (POSIX) from environment / nl_langinfo
    const char* posix_locale = setlocale(LC_ALL, nullptr);
    if (posix_locale && posix_locale[0] && std::string(posix_locale) != "C")
        lo.locale = posix_locale;
    else if (!lo.language.empty())
        lo.locale = lo.language + ".UTF-8";
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ISystem::GetNetEnvironment  –  macOS implementation
// ─────────────────────────────────────────────────────────────────────────────
ISystem::NetEnvInfo ISystem::GetNetEnvironment() {
    NetEnvInfo info;

    fill_interfaces(info);
    fill_gateway(info);
    fill_dns(info);
    fill_proxy(info);

    // TUN / VPN detection
    for (auto& iface : info.interfaces) {
        if (!iface.is_up || iface.ipv4.empty()) continue;
        if (iface.is_tun)
            info.tun_vpn.tun_ifaces.push_back(iface.name);
        if (iface.is_vpn)
            info.tun_vpn.vpn_ifaces.push_back(iface.name);
    }
    info.tun_vpn.tun_active = !info.tun_vpn.tun_ifaces.empty();
    info.tun_vpn.vpn_active = !info.tun_vpn.vpn_ifaces.empty();

    fill_tcp_stack(info);
    fill_tls_fp(info);
    fill_locale(info);

    // Connection type
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
// ISystem::is_tcp_port_available  –  macOS / POSIX implementation
// ─────────────────────────────────────────────────────────────────────────────
bool ISystem::is_tcp_port_available(const unsigned short& port,
                                    const std::string& address,
                                    std::string* out_err) {
    bool available = false;
    struct addrinfo hints{};
    struct addrinfo* res = nullptr;
    char portstr[8];
    std::snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);

    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_family   = AF_UNSPEC;

    const int gai_ret = getaddrinfo(address.c_str(), portstr, &hints, &res);
    if (gai_ret != 0) {
        if (out_err) *out_err = std::string("getaddrinfo: ") + gai_strerror(gai_ret);
        return false;
    }

    int last_err = 0;
    for (auto* p = res; p; p = p->ai_next) {
        const int s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s < 0) { last_err = errno; continue; }

        int opt = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(s, p->ai_addr, p->ai_addrlen) != 0) {
            last_err = errno; ::close(s); continue;
        }
        if (listen(s, 1) != 0) {
            last_err = errno; ::close(s); continue;
        }
        ::close(s);
        available = true;
        break;
    }
    freeaddrinfo(res);

    if (!available && out_err)
        *out_err = std::string("socket error: ") + strerror(last_err);
    return available;
}