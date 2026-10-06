#pragma once
// Passive analysis of upstream proxy HTTP CONNECT responses.

#include <cstdint>
// Extracts server identification headers and infers proxy software, OS, and type.
//
// Triggered when the upstream proxy responds to our HTTP CONNECT request.
// We parse its response headers before they are discarded and the relay begins.
//
// Usage:
//   ProxyFingerprint fp;
//   if (proxy_fp::analyze_response(buf, len, fp)) { /* fp populated */ }

#include <string>
#include <vector>

// ── ProxyFingerprint struct ───────────────────────────────────────────────────
struct ProxyFingerprint {
  // Raw status line fields
  std::string http_version;     // e.g. "1.1", "1.0"
  std::string status_code;      // e.g. "200"
  std::string reason_phrase;    // e.g. "Connection established"

  // Key response headers (raw values)
  std::string server;           // Server:
  std::string via;              // Via:
  std::string proxy_agent;      // Proxy-Agent:
  std::string x_cache;          // X-Cache:
  std::string x_powered_by;     // X-Powered-By:

  // All response headers (for custom inspection)
  std::vector<std::pair<std::string, std::string>> headers;

  // Classified results
  std::string proxy_software;   // e.g. "Squid 5.x", "CCProxy", "Nginx", …
  std::string proxy_os_hint;    // e.g. "linux", "windows", "bsd", "unknown"
  std::string proxy_type;       // e.g. "open-source/forward", "commercial/forward", "cdn", …

  bool        extracted{false};

  std::string to_json() const;
};

// ── Implementation namespace ──────────────────────────────────────────────────
namespace proxy_fp {

static inline std::string _to_lower(std::string s) {
  for (char &c : s) c = static_cast<char>(tolower((unsigned char)c));
  return s;
}
static inline std::string _trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// Minimal classification based on well-known software signatures.
struct _Cls { std::string sw, os, type; };

static inline _Cls _classify(
    const std::string &server,
    const std::string &via,
    const std::string &proxy_agent,
    const std::vector<std::pair<std::string, std::string>> &hdrs)
{
  _Cls r;
  r.os   = "unknown";
  r.type = "forward";

  auto ci = [](const std::string &hay, const std::string &needle) -> bool {
    return _to_lower(hay).find(_to_lower(needle)) != std::string::npos;
  };
  auto hdr = [&](const std::string &name) -> std::string {
    std::string nl = _to_lower(name);
    for (const auto &h : hdrs)
      if (_to_lower(h.first) == nl) return h.second;
    return {};
  };

  // ── Server header ─────────────────────────────────────────────────────────
  if      (ci(server, "squid"))          { r.sw="Squid";           r.os="linux";   r.type="open-source/forward"; }
  else if (ci(server, "tinyproxy"))      { r.sw="TinyProxy";       r.os="linux";   r.type="open-source/forward"; }
  else if (ci(server, "privoxy"))        { r.sw="Privoxy";         r.os="linux";   r.type="open-source/forward"; }
  else if (ci(server, "haproxy"))        { r.sw="HAProxy";         r.os="linux";   r.type="open-source/forward"; }
  else if (ci(server, "envoy"))          { r.sw="Envoy";           r.os="linux";   r.type="open-source/forward"; }
  else if (ci(server, "traefik"))        { r.sw="Traefik";         r.os="linux";   r.type="open-source/reverse"; }
  else if (ci(server, "caddy"))          { r.sw="Caddy";           r.os="linux";   r.type="open-source/reverse"; }
  else if (ci(server, "nginx"))          { r.sw="Nginx";           r.os="linux";   r.type="open-source/reverse"; }
  else if (ci(server, "apache"))         { r.sw="Apache";          r.os="linux";   r.type="open-source/reverse"; }
  else if (ci(server, "lighttpd"))       { r.sw="lighttpd";        r.os="linux";   r.type="open-source/reverse"; }
  else if (ci(server, "ccproxy") ||
           ci(server, "cc proxy"))       { r.sw="CCProxy";         r.os="windows"; r.type="commercial/forward"; }
  else if (ci(server, "wingate"))        { r.sw="WinGate";         r.os="windows"; r.type="commercial/forward"; }
  else if (ci(server, "microsoft-iis") ||
           ci(server, "iis/"))           { r.sw="Microsoft IIS";   r.os="windows"; r.type="commercial/reverse"; }
  else if (ci(server, "microsoft-httpapi")){ r.sw="WinHTTP/HTTPAPI"; r.os="windows"; r.type="commercial/forward"; }
  else if (ci(server, "glype"))          { r.sw="Glype Proxy";     r.os="linux";   r.type="commercial/web"; }
  else if (!server.empty())              { r.sw=server; }

  // ── Via header — can reveal additional proxy hops ─────────────────────────
  if (r.sw.empty() || r.os == "unknown") {
    if (ci(via, "squid"))   { if (r.sw.empty()) r.sw="Squid";   r.os="linux"; r.type="open-source/forward"; }
    if (ci(via, "varnish")) { if (r.sw.empty()) r.sw="Varnish"; r.os="linux"; r.type="open-source/cache"; }
  }

  // ── Proxy-Agent header (used by some commercial proxies) ─────────────────
  if (r.sw.empty() && !proxy_agent.empty()) r.sw = proxy_agent;

  // ── CDN detection via distinctive headers ─────────────────────────────────
  if (!hdr("CF-RAY").empty() || !hdr("CF-Cache-Status").empty()) {
    r.sw = "Cloudflare"; r.os = "linux"; r.type = "cdn";
  } else if (!hdr("X-Amz-Cf-Id").empty()) {
    r.sw = "AWS CloudFront"; r.os = "linux"; r.type = "cdn";
  } else if (!hdr("X-Azure-Ref").empty()) {
    r.sw = "Azure CDN"; r.os = "linux"; r.type = "cdn";
  } else if (ci(hdr("X-Cache"), "fastly")) {
    r.sw = "Fastly CDN"; r.os = "linux"; r.type = "cdn";
  }

  // ── Windows-specific application headers ─────────────────────────────────
  if (!hdr("X-AspNet-Version").empty()) r.os = "windows";
  {
    std::string xpb = hdr("X-Powered-By");
    if (ci(xpb, "ASP.NET") || ci(xpb, "asp.net"))
      r.os = "windows";
    if (ci(xpb, "PHP")) { /* PHP on any OS */ }
  }

  return r;
}

/// Parse upstream proxy HTTP CONNECT response and classify the proxy.
/// \param buf  Raw response bytes (may include header + partial body)
/// \param len  Number of bytes
/// \param out  Populated on success
/// \return true on success
inline bool analyze_response(const uint8_t *buf, size_t len, ProxyFingerprint &out) {
  if (!buf || len < 12) return false;

  const std::string raw(reinterpret_cast<const char *>(buf), len);
  auto hdr_end_pos = raw.find("\r\n\r\n");
  if (hdr_end_pos == std::string::npos) return false;

  // ── Status line ───────────────────────────────────────────────────────────
  auto line_end = raw.find("\r\n");
  if (line_end == std::string::npos) return false;
  std::string sl = raw.substr(0, line_end);

  size_t sp1 = sl.find(' ');
  if (sp1 == std::string::npos) return false;
  std::string ver_str = sl.substr(0, sp1);
  if (ver_str.size() > 5 && ver_str.substr(0, 5) == "HTTP/")
    out.http_version = ver_str.substr(5);

  size_t sp2 = sl.find(' ', sp1 + 1);
  out.status_code   = (sp2 != std::string::npos)
                      ? sl.substr(sp1 + 1, sp2 - sp1 - 1)
                      : sl.substr(sp1 + 1);
  out.reason_phrase = (sp2 != std::string::npos)
                      ? _trim(sl.substr(sp2 + 1))
                      : std::string{};

  // ── Response headers ──────────────────────────────────────────────────────
  size_t pos = line_end + 2;
  while (pos < hdr_end_pos) {
    auto next = raw.find("\r\n", pos);
    if (next == std::string::npos || next > hdr_end_pos) break;
    std::string hline = raw.substr(pos, next - pos);
    pos = next + 2;

    auto colon = hline.find(':');
    if (colon == std::string::npos) continue;
    std::string name  = _trim(hline.substr(0, colon));
    std::string value = _trim(hline.substr(colon + 1));
    out.headers.emplace_back(name, value);

    std::string nl = _to_lower(name);
    if      (nl == "server")          out.server       = value;
    else if (nl == "via")             out.via          = value;
    else if (nl == "proxy-agent")     out.proxy_agent  = value;
    else if (nl == "x-cache")         out.x_cache      = value;
    else if (nl == "x-powered-by")    out.x_powered_by = value;
  }

  // ── Classify ───────────────────────────────────────────────────────────────
  auto cls = _classify(out.server, out.via, out.proxy_agent, out.headers);
  out.proxy_software = cls.sw;
  out.proxy_os_hint  = cls.os;
  out.proxy_type     = cls.type;

  out.extracted = true;
  return true;
}

}  // namespace proxy_fp

// ── ProxyFingerprint::to_json ─────────────────────────────────────────────────
inline std::string ProxyFingerprint::to_json() const {
  auto esc = [](const std::string &s) {
    std::string r;
    for (unsigned char c : s) {
      if      (c == '"')  r += "\\\"";
      else if (c == '\\') r += "\\\\";
      else if (c == '\n') r += "\\n";
      else if (c == '\r') r += "\\r";
      else if (c < 0x20)  { char tmp[8]; snprintf(tmp,sizeof(tmp),"\\u%04x",c); r+=tmp; }
      else                r += static_cast<char>(c);
    }
    return r;
  };

  std::string j;
  j.reserve(512);
  j += "{\"extracted\":";           j += extracted ? "true" : "false";
  j += ",\"http_version\":\"";      j += esc(http_version);   j += '"';
  j += ",\"status_code\":\"";       j += esc(status_code);    j += '"';
  j += ",\"reason_phrase\":\"";     j += esc(reason_phrase);  j += '"';
  j += ",\"server\":\"";            j += esc(server);         j += '"';
  j += ",\"via\":\"";               j += esc(via);            j += '"';
  j += ",\"proxy_agent\":\"";       j += esc(proxy_agent);    j += '"';
  j += ",\"x_cache\":\"";           j += esc(x_cache);        j += '"';
  j += ",\"x_powered_by\":\"";      j += esc(x_powered_by);   j += '"';
  j += ",\"proxy_software\":\"";    j += esc(proxy_software); j += '"';
  j += ",\"proxy_os_hint\":\"";     j += esc(proxy_os_hint);  j += '"';
  j += ",\"proxy_type\":\"";        j += esc(proxy_type);     j += '"';
  // All headers
  j += ",\"response_headers\":{";
  for (size_t i = 0; i < headers.size(); ++i) {
    if (i) j += ',';
    j += '"'; j += esc(headers[i].first); j += "\":\"";
    j += esc(headers[i].second); j += '"';
  }
  j += "}}";
  return j;
}
