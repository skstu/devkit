#include <libsys.h>
#include "../net_env_impl.hpp"

#include <ifaddrs.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <locale.h>
#include <langinfo.h>
#include <time.h>
#include <dlfcn.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <map>

// ─────────────────────────────────────────────────────────────────────────────
namespace {

// ── /proc/sys helpers ────────────────────────────────────────────────────────
static std::string proc_read_str(const char* path, const char* def = "") {
    FILE* f = fopen(path, "r");
    if (!f) return def;
    char buf[256] = {};
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return def; }
    fclose(f);
    size_t l = strlen(buf);
    while (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = '\0';
    return buf;
}

static long proc_read_long(const char* path, long def = 0) {
    const std::string s = proc_read_str(path);
    if (s.empty()) return def;
    try { return std::stol(s); } catch (...) { return def; }
}

// For "min default max" triplet files like tcp_rmem / tcp_wmem.
static long proc_read_triplet_mid(const char* path, long def = 0) {
    const std::string s = proc_read_str(path);
    if (s.empty()) return def;
    std::istringstream ss(s);
    long a = 0, b = 0;
    if (!(ss >> a >> b)) return def;
    return b != 0 ? b : def;
}

// ── netmask string from CIDR prefix length ───────────────────────────────────
static std::string prefix_to_netmask(int prefix) {
    uint32_t mask = prefix ? (~0u << (32 - prefix)) : 0;
    mask = htonl(mask);
    char buf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &mask, buf, sizeof(buf));
    return buf;
}

// ── sockaddr → dotted-decimal / colon-hex string ────────────────────────────
static std::string sa_to_str(const sockaddr* sa) {
    char buf[128] = {};
    if (!sa) return {};
    if (sa->sa_family == AF_INET)
        inet_ntop(AF_INET,  &reinterpret_cast<const sockaddr_in*>(sa)->sin_addr,  buf, sizeof(buf));
    else if (sa->sa_family == AF_INET6)
        inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr, buf, sizeof(buf));
    return buf;
}

// ── Interface enumeration ────────────────────────────────────────────────────
static void fill_interfaces(ISystem::NetEnvInfo& info) {
    struct ifaddrs* ifa_head = nullptr;
    if (getifaddrs(&ifa_head) != 0) return;

    // Accumulate per-interface data across multiple AF family entries.
    std::map<std::string, ISystem::NetEnvInfo::InterfaceInfo> imap;

    for (auto* ifa = ifa_head; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name) continue;
        const std::string name = ifa->ifa_name;
        auto& iface = imap[name];
        iface.name         = name;
        iface.friendly_name = name;

        const unsigned flags = ifa->ifa_flags;
        iface.is_up       = (flags & IFF_UP)      != 0;
        iface.is_loopback = (flags & IFF_LOOPBACK) != 0;

        // TUN / TAP / WireGuard / VPN detected by name prefix
        auto starts = [&](const char* p) { return name.rfind(p, 0) == 0; };
        if (starts("tun") || starts("tap") || starts("wg") || starts("vpn"))
            iface.is_tun = true;
        if (starts("ppp") || starts("ipsec") || starts("l2tp") || starts("pptp"))
            iface.is_vpn = true;
        // POINTOPOINT flag indicates PPP / VPN / TUN
        if ((flags & IFF_POINTOPOINT) && !iface.is_tun)
            iface.is_vpn = true;

        // Wireless: presence of /sys/class/net/<name>/wireless or phy80211
        if (!iface.is_wireless) {
            struct stat st{};
            const std::string w1 = "/sys/class/net/" + name + "/wireless";
            const std::string w2 = "/sys/class/net/" + name + "/phy80211";
            if (stat(w1.c_str(), &st) == 0 || stat(w2.c_str(), &st) == 0)
                iface.is_wireless = true;
        }

        // MAC address via /sys/class/net/<name>/address
        if (iface.mac.empty()) {
            const std::string mac_path = "/sys/class/net/" + name + "/address";
            FILE* mf = fopen(mac_path.c_str(), "r");
            if (mf) {
                char mac_buf[32] = {};
                if (fgets(mac_buf, sizeof(mac_buf), mf)) {
                    size_t l = strlen(mac_buf);
                    while (l && (mac_buf[l-1] == '\n' || mac_buf[l-1] == '\r'))
                        mac_buf[--l] = '\0';
                    iface.mac = mac_buf;
                }
                fclose(mf);
            }
        }

        // MTU via /sys/class/net/<name>/mtu
        if (iface.mtu == 0) {
            const std::string mtu_path = "/sys/class/net/" + name + "/mtu";
            FILE* mf = fopen(mtu_path.c_str(), "r");
            if (mf) {
                unsigned mtu = 0;
                if (fscanf(mf, "%u", &mtu) == 1) iface.mtu = mtu;
                fclose(mf);
            }
        }

        // ARPHRD device type: 65534=ARPHRD_NONE (TUN), 768=ARPHRD_IPGRE, 776=ARPHRD_TUNNEL
        {
            const std::string type_path = "/sys/class/net/" + name + "/type";
            FILE* tf = fopen(type_path.c_str(), "r");
            if (tf) {
                int dev_type = 0;
                if (fscanf(tf, "%d", &dev_type) == 1 &&
                    (dev_type == 65534 || dev_type == 768 || dev_type == 776))
                    iface.is_tun = true;
                fclose(tf);
            }
        }

        // IPv4 address, netmask, CIDR, broadcast
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
            if (iface.ipv4.empty()) {
                iface.ipv4 = sa_to_str(ifa->ifa_addr);
                if (ifa->ifa_netmask && ifa->ifa_netmask->sa_family == AF_INET) {
                    iface.netmask = sa_to_str(ifa->ifa_netmask);
                    in_addr mask_in{};
                    inet_pton(AF_INET, iface.netmask.c_str(), &mask_in);
                    uint32_t m = ntohl(mask_in.s_addr);
                    int prefix = 0;
                    while (m & 0x80000000u) { ++prefix; m <<= 1; }
                    iface.ipv4_cidr = iface.ipv4 + "/" + std::to_string(prefix);
                }
                if (ifa->ifa_broadaddr && !(flags & IFF_POINTOPOINT))
                    iface.broadcast = sa_to_str(ifa->ifa_broadaddr);
            }
        } else if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET6) {
            if (iface.ipv6.empty())
                iface.ipv6 = sa_to_str(ifa->ifa_addr);
        }
    }
    freeifaddrs(ifa_head);

    for (auto& kv : imap)
        info.interfaces.push_back(kv.second);
}

// ── Default gateway via /proc/net/route ──────────────────────────────────────
// Columns: Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT
// All numeric values are in host-byte-order hex on any Linux architecture.
static void fill_gateway(ISystem::NetEnvInfo& info) {
    FILE* f = fopen("/proc/net/route", "r");
    if (f) {
        char line[256];
        fgets(line, sizeof(line), f);   // skip header
        while (fgets(line, sizeof(line), f)) {
            char iface_name[32] = {};
            unsigned dest = 0, gw = 0, flags = 0;
            if (sscanf(line, "%31s %x %x %x", iface_name, &dest, &gw, &flags) < 4)
                continue;
            // Default route: Destination == 0  AND  RTF_GATEWAY (0x2) set
            if (dest == 0 && (flags & 0x2) && info.default_gateway_ipv4.empty()) {
                in_addr gw_addr{};
                gw_addr.s_addr = gw;   // host-byte-order direct assignment
                char gbuf[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, &gw_addr, gbuf, sizeof(gbuf));
                info.default_gateway_ipv4 = gbuf;
                info.egress_interface     = iface_name;
                for (auto& iif : info.interfaces) {
                    if (iif.name == iface_name && !iif.ipv4.empty()) {
                        info.egress_ip = iif.ipv4;
                        break;
                    }
                }
            }
        }
        fclose(f);
    }

    // Fallback: first UP non-loopback non-TUN interface with an IPv4 address
    if (info.egress_ip.empty()) {
        for (auto& iface : info.interfaces) {
            if (!iface.is_loopback && iface.is_up && !iface.ipv4.empty() && !iface.is_tun) {
                info.egress_interface = iface.name;
                info.egress_ip        = iface.ipv4;
                break;
            }
        }
    }
}

// ── DNS servers ───────────────────────────────────────────────────────────────
static void fill_dns(ISystem::NetEnvInfo& info) {
    // Prefer systemd-resolved's full list; fall back to /etc/resolv.conf.
    const char* candidates[] = {
        "/run/systemd/resolve/resolv.conf",
        "/etc/resolv.conf",
        nullptr
    };
    for (const char** p = candidates; *p; ++p) {
        FILE* f = fopen(*p, "r");
        if (!f) continue;
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "nameserver", 10) != 0) continue;
            char ns[64] = {};
            if (sscanf(line + 10, " %63s", ns) != 1) continue;
            bool dup = false;
            for (auto& d : info.dns_servers) if (d == ns) { dup = true; break; }
            if (!dup) info.dns_servers.emplace_back(ns);
        }
        fclose(f);
        if (!info.dns_servers.empty()) break;
    }
}

// ── System proxy ─────────────────────────────────────────────────────────────
// Priority: all_proxy > https_proxy > http_proxy > socks_proxy (env vars).
// Fallback: KDE kioslaverc (~/.config/kioslaverc).
static void fill_proxy(ISystem::NetEnvInfo& info) {
    auto& proxy = info.proxy;

    // Return first non-empty of lowercase / uppercase env var pair.
    auto get_env = [](const char* lo, const char* hi) -> std::string {
        const char* v = getenv(lo);
        if (!v || !v[0]) v = getenv(hi);
        return (v && v[0]) ? v : std::string{};
    };

    // Parse a proxy URL into the ProxyInfo fields.
    auto parse_url = [&](const std::string& raw_url, const std::string& hint_scheme) {
        std::string url   = raw_url;
        std::string scheme;
        const auto p2 = url.find("://");
        if (p2 != std::string::npos) {
            scheme = url.substr(0, p2);
            url    = url.substr(p2 + 3);
        } else {
            scheme = hint_scheme;
        }
        for (auto& c : scheme) c = (char)tolower((unsigned char)c);

        // Strip user:pass@
        const auto at = url.rfind('@');
        if (at != std::string::npos) url = url.substr(at + 1);
        // Strip trailing path
        const auto sl = url.find('/');
        if (sl != std::string::npos) url = url.substr(0, sl);

        const auto col = url.rfind(':');
        proxy.host = (col != std::string::npos) ? url.substr(0, col) : url;
        if (col != std::string::npos)
            try { proxy.port = static_cast<uint16_t>(std::stoul(url.substr(col + 1))); }
            catch (...) {}

        if      (scheme == "socks5" || scheme == "socks")
            { proxy.type = ISystem::NetEnvInfo::ProxyInfo::Type::SOCKS5; proxy.type_str = "SOCKS5"; }
        else if (scheme == "socks4")
            { proxy.type = ISystem::NetEnvInfo::ProxyInfo::Type::SOCKS4; proxy.type_str = "SOCKS4"; }
        else if (scheme == "https")
            { proxy.type = ISystem::NetEnvInfo::ProxyInfo::Type::HTTPS;  proxy.type_str = "HTTPS"; }
        else
            { proxy.type = ISystem::NetEnvInfo::ProxyInfo::Type::HTTP;   proxy.type_str = "HTTP"; }
    };

    std::string proxy_url;
    if      (!(proxy_url = get_env("all_proxy",   "ALL_PROXY")).empty())   parse_url(proxy_url, "http");
    else if (!(proxy_url = get_env("https_proxy", "HTTPS_PROXY")).empty()) parse_url(proxy_url, "https");
    else if (!(proxy_url = get_env("http_proxy",  "HTTP_PROXY")).empty())  parse_url(proxy_url, "http");
    else if (!(proxy_url = get_env("socks_proxy", "SOCKS_PROXY")).empty()) parse_url(proxy_url, "socks5");

    if (proxy.type != ISystem::NetEnvInfo::ProxyInfo::Type::None) {
        // NO_PROXY bypass list
        const char* no_proxy = getenv("no_proxy");
        if (!no_proxy || !no_proxy[0]) no_proxy = getenv("NO_PROXY");
        if (no_proxy && no_proxy[0]) {
            std::istringstream ss(no_proxy);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                while (!tok.empty() && isspace((unsigned char)tok.front())) tok.erase(tok.begin());
                while (!tok.empty() && isspace((unsigned char)tok.back()))  tok.pop_back();
                if (!tok.empty()) proxy.bypass_list.push_back(tok);
            }
        }
        return;
    }

    // ── KDE kioslaverc fallback ──────────────────────────────────────────────
    const char* home = getenv("HOME");
    if (home) {
        const std::string krc = std::string(home) + "/.config/kioslaverc";
        FILE* f = fopen(krc.c_str(), "r");
        if (f) {
            char line[512];
            bool in_sec = false;
            std::string kde_type;
            std::string kde_http_host;
            uint16_t    kde_http_port = 0;

            while (fgets(line, sizeof(line), f)) {
                std::string l = line;
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
                if (l == "[Proxy Settings]") { in_sec = true; continue; }
                if (!l.empty() && l[0] == '[') { in_sec = false; continue; }
                if (!in_sec) continue;
                const auto eq = l.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = l.substr(0, eq);
                const std::string val = l.substr(eq + 1);

                if (key == "ProxyType") {
                    kde_type = val;
                } else if ((key == "httpProxy" || key == "httpsProxy") &&
                           kde_http_host.empty()) {
                    // Formats: "http://host:port" or "http://host port"
                    std::string hp = val;
                    uint16_t    hp_port = 0;
                    const auto sp2 = hp.rfind("://");
                    if (sp2 != std::string::npos) hp = hp.substr(sp2 + 3);
                    const auto space = hp.rfind(' ');
                    if (space != std::string::npos) {
                        try { hp_port = static_cast<uint16_t>(std::stoul(hp.substr(space + 1))); }
                        catch (...) {}
                        hp = hp.substr(0, space);
                    }
                    const auto colon = hp.rfind(':');
                    if (colon != std::string::npos) {
                        if (!hp_port)
                            try { hp_port = static_cast<uint16_t>(std::stoul(hp.substr(colon + 1))); }
                            catch (...) {}
                        kde_http_host = hp.substr(0, colon);
                    } else {
                        kde_http_host = hp;
                    }
                    kde_http_port = hp_port;
                } else if (key == "socksProxy" && proxy.host.empty()) {
                    std::string hp = val;
                    const auto sp2 = hp.rfind("://");
                    if (sp2 != std::string::npos) hp = hp.substr(sp2 + 3);
                    const auto space = hp.rfind(' ');
                    uint16_t sp_port = 0;
                    if (space != std::string::npos) {
                        try { sp_port = static_cast<uint16_t>(std::stoul(hp.substr(space + 1))); }
                        catch (...) {}
                        hp = hp.substr(0, space);
                    }
                    const auto colon = hp.rfind(':');
                    std::string sp_host;
                    if (colon != std::string::npos) {
                        if (!sp_port)
                            try { sp_port = static_cast<uint16_t>(std::stoul(hp.substr(colon + 1))); }
                            catch (...) {}
                        sp_host = hp.substr(0, colon);
                    } else {
                        sp_host = hp;
                    }
                    if (!sp_host.empty()) {
                        proxy.host     = sp_host;
                        proxy.port     = sp_port;
                        proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::SOCKS5;
                        proxy.type_str = "SOCKS5";
                    }
                }
            }
            fclose(f);

            // ProxyType==1 → manual proxy
            if (kde_type == "1" && !kde_http_host.empty() &&
                proxy.type == ISystem::NetEnvInfo::ProxyInfo::Type::None) {
                proxy.host     = kde_http_host;
                proxy.port     = kde_http_port;
                proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::HTTP;
                proxy.type_str = "HTTP";
            } else if (kde_type == "3") {    // PAC
                proxy.type     = ISystem::NetEnvInfo::ProxyInfo::Type::PAC;
                proxy.type_str = "PAC";
            } else if (kde_type == "4") {    // WPAD / auto-detect
                proxy.auto_detect = true;
                proxy.type        = ISystem::NetEnvInfo::ProxyInfo::Type::AutoDetect;
                proxy.type_str    = "AutoDetect";
            }
        }
    }

    if (proxy.type == ISystem::NetEnvInfo::ProxyInfo::Type::None)
        proxy.type_str = "None";
}

// ── TCP/IP stack parameters ───────────────────────────────────────────────────
static void fill_tcp_stack(ISystem::NetEnvInfo& info) {
    auto& ts = info.tcp_stack;

    ts.default_ttl      = static_cast<uint32_t>(proc_read_long("/proc/sys/net/ipv4/ip_default_ttl",   64));
    ts.recv_window_size = static_cast<uint32_t>(proc_read_triplet_mid("/proc/sys/net/ipv4/tcp_rmem",   87380));
    ts.send_buffer_size = static_cast<uint32_t>(proc_read_triplet_mid("/proc/sys/net/ipv4/tcp_wmem",   65536));
    ts.window_scaling   = proc_read_long("/proc/sys/net/ipv4/tcp_window_scaling", 1) != 0;
    ts.sack_enabled     = proc_read_long("/proc/sys/net/ipv4/tcp_sack",           1) != 0;
    ts.timestamps       = proc_read_long("/proc/sys/net/ipv4/tcp_timestamps",     1) != 0;
    ts.ecn_enabled      = proc_read_long("/proc/sys/net/ipv4/tcp_ecn",            0) != 0;
    ts.keepalive_time_sec  = static_cast<uint32_t>(proc_read_long("/proc/sys/net/ipv4/tcp_keepalive_time",   7200));
    ts.keepalive_intvl_sec = static_cast<uint32_t>(proc_read_long("/proc/sys/net/ipv4/tcp_keepalive_intvl",  75));
    ts.keepalive_probes    = static_cast<uint8_t> (proc_read_long("/proc/sys/net/ipv4/tcp_keepalive_probes", 9));
    ts.mss = 1460;   // standard Ethernet MSS

    const long rmem_max = proc_read_long("/proc/sys/net/core/rmem_max", 212992);
    ts.max_window_size  = static_cast<uint32_t>(rmem_max > 0 ? rmem_max : 212992);

    // Window scale exponent = log2(recv_window / 65535), capped at 14
    uint32_t scale = 0, w = ts.recv_window_size;
    while (w > 65535u && scale < 14u) { w >>= 1; ++scale; }
    ts.window_scale = scale;
}

// ── TLS fingerprint ───────────────────────────────────────────────────────────
// Attempts to enumerate ciphers by dynamically loading libssl (OpenSSL 3 / 1.1.1).
// Falls back to a representative OpenSSL 3.x DEFAULT cipher list.
static void fill_tls_fp(ISystem::NetEnvInfo& info) {
    auto& fp = info.tls_fp;

    // Any modern Linux distro ships OpenSSL ≥ 1.1.1 which supports TLS 1.3.
    fp.tls_version_max = 0x0304;
    fp.tls_version_str = tls_version_str(fp.tls_version_max);

    // Dynamic libssl enumeration ─────────────────────────────────────────────
    bool enumerated = false;
    void* libssl = dlopen("libssl.so.3", RTLD_LAZY | RTLD_LOCAL);
    if (!libssl) libssl = dlopen("libssl.so.1.1", RTLD_LAZY | RTLD_LOCAL);
    if (libssl) {
        typedef void* (*TLS_client_method_fn)();
        typedef void* (*SSL_CTX_new_fn)(void*);
        typedef void  (*SSL_CTX_free_fn)(void*);
        typedef void* (*SSL_CTX_get_ciphers_fn)(const void*);
        typedef int   (*sk_SSL_CIPHER_num_fn)(const void*);
        typedef void* (*sk_SSL_CIPHER_value_fn)(const void*, int);
        typedef uint16_t (*SSL_CIPHER_get_protocol_id_fn)(const void*);

        auto f_method = (TLS_client_method_fn)        dlsym(libssl, "TLS_client_method");
        auto f_new    = (SSL_CTX_new_fn)              dlsym(libssl, "SSL_CTX_new");
        auto f_free   = (SSL_CTX_free_fn)             dlsym(libssl, "SSL_CTX_free");
        auto f_gcip   = (SSL_CTX_get_ciphers_fn)      dlsym(libssl, "SSL_CTX_get_ciphers");
        auto f_sknum  = (sk_SSL_CIPHER_num_fn)        dlsym(libssl, "sk_SSL_CIPHER_num");
        auto f_skval  = (sk_SSL_CIPHER_value_fn)      dlsym(libssl, "sk_SSL_CIPHER_value");
        auto f_getid  = (SSL_CIPHER_get_protocol_id_fn) dlsym(libssl, "SSL_CIPHER_get_protocol_id");

        if (f_method && f_new && f_free && f_gcip && f_sknum && f_skval && f_getid) {
            void* method = f_method();
            void* ctx    = method ? f_new(method) : nullptr;
            if (ctx) {
                void* stack = f_gcip(ctx);
                if (stack) {
                    const int n = f_sknum(stack);
                    for (int i = 0; i < n; i++) {
                        void* cipher = f_skval(stack, i);
                        if (!cipher) continue;
                        const uint16_t id = f_getid(cipher);
                        if (id && !nenv_detail::is_grease(id))
                            fp.cipher_suites.push_back(id);
                    }
                    enumerated = !fp.cipher_suites.empty();
                }
                f_free(ctx);
            }
        }
        dlclose(libssl);
    }

    if (!enumerated) {
        // Representative OpenSSL 3.x DEFAULT cipher list (TLS 1.3 first, then TLS 1.2)
        fp.cipher_suites = {
            0x1302, 0x1303, 0x1301,              // TLS_AES_{256,128}_GCM, CHACHA20
            0xC02C, 0xC030, 0x009F,              // ECDHE-ECDSA/RSA-AES256-GCM, DHE-RSA-AES256-GCM
            0xCCA9, 0xCCA8, 0xCCAA,              // ChaCha20-Poly1305 variants
            0xC02B, 0xC02F, 0x009E,              // ECDHE-ECDSA/RSA-AES128-GCM, DHE-RSA-AES128-GCM
            0xC024, 0xC028, 0x006B,              // ECDHE-ECDSA/RSA-AES256-SHA384, DHE-RSA-AES256-SHA256
            0xC023, 0xC027, 0x0067,              // ECDHE-ECDSA/RSA-AES128-SHA256, DHE-RSA-AES128-SHA256
            0xC00A, 0xC014, 0x0039,              // ECDHE-ECDSA/RSA-AES256-SHA, DHE-RSA-AES256-SHA
            0xC009, 0xC013, 0x0033,              // ECDHE-ECDSA/RSA-AES128-SHA, DHE-RSA-AES128-SHA
            0x009D, 0x009C,                      // AES256/128-GCM-SHA384/256
            0x003D, 0x003C, 0x0035, 0x002F,      // AES256/128-SHA256/SHA
        };
    }

    // Standard OpenSSL / Linux TLS extension order
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
        35,     // session_ticket
        43,     // supported_versions
        45,     // psk_key_exchange_modes
        51,     // key_share
        65281,  // renegotiation_info
    };

    // Supported groups: OpenSSL 3.x preffers x25519, then NIST curves, then FFDHE
    fp.groups        = { 29, 23, 24, 25, 256, 257 };
    fp.ec_point_fmts = { 0 };           // uncompressed only
    fp.alpn          = { "h2", "http/1.1" };

    compute_tls_fingerprints(fp);
}

// ── Locale / timezone / language ─────────────────────────────────────────────
static void fill_locale(ISystem::NetEnvInfo& info) {
    auto& lo = info.locale;

    // Timezone name ─────────────────────────────────────────────────────────
    // 1. TZ environment variable
    const char* tz_env = getenv("TZ");
    if (tz_env && tz_env[0]) lo.timezone_name = tz_env;

    // 2. /etc/timezone  (Debian / Ubuntu)
    if (lo.timezone_name.empty()) {
        FILE* f = fopen("/etc/timezone", "r");
        if (f) {
            char buf[128] = {};
            if (fgets(buf, sizeof(buf), f)) {
                size_t l = strlen(buf);
                while (l && (buf[l-1] == '\n' || buf[l-1] == '\r')) buf[--l] = '\0';
                lo.timezone_name = buf;
            }
            fclose(f);
        }
    }

    // 3. /etc/localtime symlink target  (RHEL / Arch / Alpine / Gentoo)
    if (lo.timezone_name.empty()) {
        char link_target[512] = {};
        const ssize_t r = readlink("/etc/localtime", link_target, sizeof(link_target) - 1);
        if (r > 0) {
            link_target[r] = '\0';
            const char* marker = "/zoneinfo/";
            const char* pos    = strstr(link_target, marker);
            if (pos) lo.timezone_name = pos + strlen(marker);
        }
    }

    // UTC offset and abbreviation via localtime_r ──────────────────────────
    {
        tzset();
        time_t t = time(nullptr);
        struct tm tm_local{};
        localtime_r(&t, &tm_local);
        lo.utc_offset_sec = static_cast<int>(tm_local.tm_gmtoff);

        const int abs_s = lo.utc_offset_sec < 0 ? -lo.utc_offset_sec : lo.utc_offset_sec;
        char offbuf[16];
        std::snprintf(offbuf, sizeof(offbuf), "%c%02d:%02d",
                      lo.utc_offset_sec < 0 ? '-' : '+',
                      abs_s / 3600, (abs_s % 3600) / 60);
        lo.utc_offset_str = offbuf;

        if (tm_local.tm_zone && tm_local.tm_zone[0])
            lo.timezone_abbr = tm_local.tm_zone;
    }

    // Language and locale from POSIX environment variables ─────────────────
    {
        const char* lc_all = getenv("LC_ALL");
        const char* lang   = getenv("LANG");
        const char* lc_msg = getenv("LC_MESSAGES");

        std::string locale_str;
        if      (lc_all && lc_all[0]) locale_str = lc_all;
        else if (lang    && lang[0])   locale_str = lang;
        else if (lc_msg  && lc_msg[0]) locale_str = lc_msg;

        if (!locale_str.empty()) {
            lo.locale = locale_str;
            // Strip encoding suffix: "zh_CN.UTF-8" → "zh_CN"
            std::string base = locale_str;
            const auto dot = base.find('.');
            if (dot != std::string::npos) base = base.substr(0, dot);
            // "zh_CN" → language="zh-CN", country_code="CN"
            const auto us = base.find('_');
            if (us != std::string::npos) {
                lo.language     = base.substr(0, us) + "-" + base.substr(us + 1);
                lo.country_code = base.substr(us + 1);
            } else {
                lo.language = base;
            }
        } else {
            const char* cl = setlocale(LC_ALL, nullptr);
            if (cl && cl[0] && std::string(cl) != "C" && std::string(cl) != "POSIX")
                lo.locale = cl;
            lo.language = "en-US";
        }
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ISystem::GetNetEnvironment  –  Linux implementation
// ─────────────────────────────────────────────────────────────────────────────
ISystem::NetEnvInfo ISystem::GetNetEnvironment() {
    NetEnvInfo info;

    fill_interfaces(info);
    fill_gateway(info);
    fill_dns(info);
    fill_proxy(info);

    // TUN / VPN detection across all UP interfaces
    for (auto& iface : info.interfaces) {
        if (!iface.is_up) continue;
        if (iface.is_tun) info.tun_vpn.tun_ifaces.push_back(iface.name);
        if (iface.is_vpn) info.tun_vpn.vpn_ifaces.push_back(iface.name);
    }
    info.tun_vpn.tun_active = !info.tun_vpn.tun_ifaces.empty();
    info.tun_vpn.vpn_active = !info.tun_vpn.vpn_ifaces.empty();

    fill_tcp_stack(info);
    fill_tls_fp(info);
    fill_locale(info);

    // Connection type classification
    if (info.tun_vpn.tun_active)
        { info.connection_type = NetEnvInfo::ConnType::TUN;   info.connection_type_str = "TUN"; }
    else if (info.tun_vpn.vpn_active)
        { info.connection_type = NetEnvInfo::ConnType::VPN;   info.connection_type_str = "VPN"; }
    else if (info.proxy.type != NetEnvInfo::ProxyInfo::Type::None)
        { info.connection_type = NetEnvInfo::ConnType::Proxy; info.connection_type_str = "Proxy"; }
    else
        { info.connection_type = NetEnvInfo::ConnType::Direct; info.connection_type_str = "Direct"; }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// ISystem::is_tcp_port_available  –  Linux / POSIX implementation
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
