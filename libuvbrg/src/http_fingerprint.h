#pragma once
// Passive HTTP request header analysis: JA4H fingerprint + OS/browser detection.
// Extracts characteristic patterns that reveal client identity from HTTP headers.
//
// JA4H format (FoxIO spec):
//   {method2}{ver2}{cookie}{referer}{hdr_count:02d}{lang4}_{hdr_order_hash}_{cookie_hash}
//
// Usage:
//   HttpFingerprint fp;
//   if (http_fp::analyze(method, version, headers, fp)) { /* fp populated */ }

#include <openssl/evp.h>

#include <algorithm>
#include <string>
#include <vector>

// ── HttpFingerprint struct ─────────────────────────────────────────────────────
struct HttpFingerprint {
  // JA4H components
  std::string method;
  std::string http_version;        // "10", "11", "20"
  bool        has_cookie{false};
  bool        has_referer{false};
  int         header_count{0};
  std::string accept_lang_prefix;  // first 4 chars, "0000" if absent

  // Detected client characteristics
  std::string user_agent;
  std::string detected_os;         // e.g. "Windows 10/11", "macOS 14.0", "Android 13"
  std::string detected_browser;    // e.g. "Chrome 120", "Firefox 121"
  std::string accept_language;     // raw Accept-Language value

  // Fingerprints
  std::string ja4h;                // JA4H fingerprint string
  std::string header_order_hash;   // first 12 of SHA-256(ordered header names)
  std::string cookie_hash;         // first 12 of SHA-256(sorted cookie names)

  // Raw header order (names only, in original sequence)
  std::vector<std::string> header_order;

  bool extracted{false};

  std::string to_json() const;
};

// ── Implementation namespace ──────────────────────────────────────────────────
namespace http_fp {

// ── Internal helpers ──────────────────────────────────────────────────────────
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

// SHA-256(input), return first 12 hex characters.
static inline std::string _sha256_12(const std::string &input) {
  uint8_t      digest[32] = {};
  unsigned int dlen       = 0;
  EVP_MD_CTX  *ctx        = EVP_MD_CTX_new();
  std::string  r          = "000000000000";
  if (ctx) {
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
        EVP_DigestUpdate(ctx, input.data(), input.size()) == 1 &&
        EVP_DigestFinal_ex(ctx, digest, &dlen) == 1) {
      char hex[65] = {};
      for (unsigned i = 0; i < dlen; ++i)
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
      r = std::string(hex, 12);
    }
    EVP_MD_CTX_free(ctx);
  }
  return r;
}

// ── OS detection from User-Agent string ───────────────────────────────────────
static inline std::string _detect_os(const std::string &ua) {
  if (ua.empty()) return "unknown";

  auto find_ci = [&](const std::string &needle) -> size_t {
    return _to_lower(ua).find(_to_lower(needle));
  };

  // Extract version token that follows a known prefix, terminated by ';', ')', ' '.
  auto extract_ver = [&](const std::string &prefix) -> std::string {
    size_t p = find_ci(prefix);
    if (p == std::string::npos) return "";
    size_t s = p + prefix.size();
    while (s < ua.size() && ua[s] == ' ') ++s;
    size_t e = s;
    while (e < ua.size() && ua[e] != ';' && ua[e] != ')' && ua[e] != ' ')
      ++e;
    std::string v = ua.substr(s, e - s);
    for (char &c : v) if (c == '_') c = '.';
    return v;
  };

  // Android must be checked before generic Linux
  if (find_ci("Android") != std::string::npos) {
    std::string v = extract_ver("Android ");
    return v.empty() ? "Android" : "Android " + v;
  }
  if (find_ci("iPhone") != std::string::npos ||
      find_ci("iPad")   != std::string::npos ||
      find_ci("iPod")   != std::string::npos) {
    std::string v = extract_ver("CPU iPhone OS ");
    if (v.empty()) v = extract_ver("CPU OS ");
    return v.empty() ? "iOS" : "iOS " + v;
  }
  if (find_ci("CrOS") != std::string::npos)
    return "ChromeOS";

  if (find_ci("Windows NT") != std::string::npos) {
    std::string v = extract_ver("Windows NT ");
    static const struct { const char *nt; const char *name; } kNT[] = {
      {"10.0","Windows 10/11"}, {"6.3","Windows 8.1"}, {"6.2","Windows 8"},
      {"6.1","Windows 7"},      {"6.0","Windows Vista"},{"5.1","Windows XP"},
    };
    for (auto &e : kNT) if (v == e.nt) return e.name;
    return v.empty() ? "Windows" : "Windows NT " + v;
  }
  if (find_ci("Macintosh") != std::string::npos ||
      find_ci("Mac OS X")  != std::string::npos) {
    std::string v = extract_ver("Mac OS X ");
    return v.empty() ? "macOS" : "macOS " + v;
  }
  if (find_ci("Linux") != std::string::npos) {
    if (find_ci("Ubuntu") != std::string::npos) return "Ubuntu Linux";
    if (find_ci("Fedora") != std::string::npos) return "Fedora Linux";
    if (find_ci("Debian") != std::string::npos) return "Debian Linux";
    return "Linux";
  }
  return "unknown";
}

// ── Browser detection from User-Agent ─────────────────────────────────────────
static inline std::string _detect_browser(const std::string &ua) {
  if (ua.empty()) return "unknown";

  auto find_ci = [&](const std::string &n) -> size_t {
    return _to_lower(ua).find(_to_lower(n));
  };
  auto major_ver = [&](const std::string &prefix) -> std::string {
    size_t p = find_ci(prefix);
    if (p == std::string::npos) return "?";
    size_t s = p + prefix.size(), e = s;
    while (e < ua.size() && ua[e] != '.' && ua[e] != ' ' && ua[e] != ';') ++e;
    return ua.substr(s, e - s);
  };

  // Order is important: Edge/OPR before Chrome (they embed Chrome's token)
  if (find_ci("Edg/")  != std::string::npos) return "Edge "           + major_ver("Edg/");
  if (find_ci("EdgA/") != std::string::npos) return "Edge(Android) "  + major_ver("EdgA/");
  if (find_ci("OPR/")  != std::string::npos) return "Opera "          + major_ver("OPR/");
  if (find_ci("SamsungBrowser/") != std::string::npos)
                                             return "Samsung "         + major_ver("SamsungBrowser/");
  if (find_ci("Chrome/") != std::string::npos) return "Chrome "       + major_ver("Chrome/");
  if (find_ci("Firefox/")!= std::string::npos) return "Firefox "      + major_ver("Firefox/");
  if (find_ci("Safari/") != std::string::npos &&
      find_ci("Version/")!= std::string::npos) return "Safari "       + major_ver("Version/");
  if (find_ci("curl/")              != std::string::npos) return "curl";
  if (find_ci("python-requests")    != std::string::npos) return "Python/requests";
  if (find_ci("go-http-client")     != std::string::npos) return "Go/http";
  if (find_ci("okhttp/")            != std::string::npos) return "OkHttp " + major_ver("okhttp/");
  if (find_ci("java/")              != std::string::npos) return "Java "    + major_ver("Java/");
  return "other";
}

// ── Parse cookie header: return sorted field names ─────────────────────────────
static inline std::vector<std::string> _cookie_names(const std::string &hdr) {
  std::vector<std::string> names;
  size_t pos = 0;
  while (pos < hdr.size()) {
    size_t eq = hdr.find('=', pos);
    if (eq == std::string::npos) break;
    std::string name = _trim(hdr.substr(pos, eq - pos));
    if (!name.empty()) names.push_back(name);
    size_t sc = hdr.find(';', eq + 1);
    pos = (sc == std::string::npos) ? hdr.size() : sc + 1;
  }
  return names;
}

// ── HTTP version → 2-char code ────────────────────────────────────────────────
static inline std::string _ver_code(const std::string &ver) {
  if (ver == "HTTP/1.1" || ver == "1.1") return "11";
  if (ver == "HTTP/2"   || ver == "2"  || ver == "HTTP/2.0") return "20";
  if (ver == "HTTP/3"   || ver == "3"  || ver == "HTTP/3.0") return "30";
  if (ver == "HTTP/1.0" || ver == "1.0") return "10";
  return "00";
}

// ── Method → 2-char lowercase code ────────────────────────────────────────────
static inline std::string _method_code(const std::string &m) {
  std::string lm = _to_lower(m);
  if (lm.size() >= 2) return lm.substr(0, 2);
  while (lm.size() < 2) lm += '0';
  return lm;
}

// ── Accept-Language first 4 chars (or "0000") ─────────────────────────────────
static inline std::string _lang_prefix4(const std::string &al) {
  if (al.empty()) return "0000";
  std::string r;
  for (char c : al) {
    if (c != ' ' && c != '\t') r += c;
    if (r.size() >= 4) break;
  }
  while (r.size() < 4) r += '0';
  return r.substr(0, 4);
}

// ── Main analysis entry point ─────────────────────────────────────────────────
/// Compute JA4H and detect OS/browser from parsed HTTP request headers.
/// \param method      HTTP method (e.g. "GET", "CONNECT")
/// \param http_version  Raw version string (e.g. "HTTP/1.1")
/// \param headers     Vector of (name, value) pairs in original order
/// \param out         Output structure
/// \return true on success
inline bool analyze(
    const std::string &method,
    const std::string &http_version,
    const std::vector<std::pair<std::string, std::string>> &headers,
    HttpFingerprint &out)
{
  if (method.empty()) return false;

  out.method       = method;
  out.http_version = _ver_code(http_version);

  std::string cookie_value;
  std::string accept_lang;
  std::string user_agent;

  for (const auto &h : headers) {
    std::string name = _trim(h.first);
    std::string nl   = _to_lower(name);
    out.header_order.push_back(name);

    if (nl == "cookie")          { out.has_cookie   = true; cookie_value = h.second; }
    if (nl == "referer" ||
        nl == "referrer")        { out.has_referer  = true; }
    if (nl == "accept-language") { accept_lang = h.second; }
    if (nl == "user-agent")      { user_agent  = h.second; }
  }

  out.header_count      = static_cast<int>(headers.size());
  out.accept_lang_prefix = _lang_prefix4(accept_lang);
  out.accept_language   = accept_lang;
  out.user_agent        = user_agent;

  // OS / browser detection from User-Agent
  out.detected_os      = _detect_os(user_agent);
  out.detected_browser = _detect_browser(user_agent);

  // ── JA4H Part a: {method2}{ver2}{cookie}{referer}{cnt:02d}{lang4} ──────
  char part_a[32] = {};
  snprintf(part_a, sizeof(part_a), "%s%s%c%c%02d%s",
           _method_code(method).c_str(),
           out.http_version.c_str(),
           out.has_cookie  ? 'c' : 'n',
           out.has_referer ? 'r' : 'n',
           out.header_count,
           out.accept_lang_prefix.c_str());

  // ── JA4H Part b: SHA-256-12 of ordered header names, comma-joined lowercase ─
  std::string hnames;
  for (size_t i = 0; i < out.header_order.size(); ++i) {
    if (i) hnames += ',';
    hnames += _to_lower(out.header_order[i]);
  }
  out.header_order_hash = _sha256_12(hnames);

  // ── JA4H Part c: SHA-256-12 of sorted cookie field names ─────────────────
  if (!cookie_value.empty()) {
    auto names = _cookie_names(cookie_value);
    std::sort(names.begin(), names.end());
    std::string cstr;
    for (size_t i = 0; i < names.size(); ++i) {
      if (i) cstr += ',';
      cstr += names[i];
    }
    out.cookie_hash = _sha256_12(cstr);
  } else {
    out.cookie_hash = "000000000000";
  }

  out.ja4h      = std::string(part_a) + '_' + out.header_order_hash + '_' + out.cookie_hash;
  out.extracted = true;
  return true;
}

} // namespace http_fp

// ── HttpFingerprint::to_json ──────────────────────────────────────────────────
inline std::string HttpFingerprint::to_json() const {
  auto esc = [](const std::string &s) {
    std::string r;
    r.reserve(s.size() + 8);
    for (unsigned char c : s) {
      if (c == '"')       r += "\\\"";
      else if (c == '\\') r += "\\\\";
      else if (c == '\n') r += "\\n";
      else if (c == '\r') r += "\\r";
      else if (c < 0x20)  { char tmp[8]; snprintf(tmp,sizeof(tmp),"\\u%04x",c); r+=tmp; }
      else                r += static_cast<char>(c);
    }
    return r;
  };

  std::string j;
  j.reserve(768);
  j += "{\"extracted\":";       j += extracted ? "true" : "false";
  j += ",\"method\":\"";        j += esc(method);           j += '"';
  j += ",\"http_version\":\"";  j += esc(http_version);     j += '"';
  j += ",\"has_cookie\":";      j += has_cookie  ? "true" : "false";
  j += ",\"has_referer\":";     j += has_referer ? "true" : "false";
  j += ",\"header_count\":";    j += std::to_string(header_count);
  j += ",\"accept_language\":\""; j += esc(accept_language);j += '"';
  j += ",\"detected_os\":\"";   j += esc(detected_os);      j += '"';
  j += ",\"detected_browser\":\""; j += esc(detected_browser); j += '"';
  j += ",\"user_agent\":\"";    j += esc(user_agent);        j += '"';
  j += ",\"ja4h\":\"";          j += esc(ja4h);              j += '"';
  j += ",\"header_order_hash\":\""; j += esc(header_order_hash); j += '"';
  j += ",\"cookie_hash\":\"";   j += esc(cookie_hash);       j += '"';
  // Header order array
  j += ",\"header_order\":[";
  for (size_t i = 0; i < header_order.size(); ++i) {
    if (i) j += ',';
    j += '"'; j += esc(header_order[i]); j += '"';
  }
  j += "]}";
  return j;
}
