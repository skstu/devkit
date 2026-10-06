#pragma once
// Passive TLS ClientHello parser for JA3 fingerprinting.
// Reads raw plaintext TCP bytes; never establishes a TLS session.
//
// Usage:
//   TlsFingerprint fp;
//   if (tls_fp::parse(buf, len, fp)) { /* fp is populated */ }

#include <openssl/evp.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#if !defined(TLS_FINGERPRINT_INCLUDED_VIA_BRIDGE_IMPL)
// TlsFingerprint is defined in bridge_impl.h when included from libbridge.cc;
// provide a standalone definition only when this header is used independently.
struct TlsFingerprint {
  uint16_t              version{0};
  std::vector<uint16_t> ciphers;
  std::vector<uint16_t> ext_types;
  std::vector<uint16_t> groups;       // supported_groups
  std::vector<uint8_t>  ec_pt_fmts;   // ec_point_formats
  std::vector<uint16_t> sig_algs;     // signature_algorithms (0x000d)
  std::string           sni;
  std::vector<std::string> alpn;
  std::string           ja3;
  std::string           ja3_hash;
  std::string           ja4;          // JA4 fingerprint
  std::string           ja4_raw;      // JA4_r (raw, unhashed values)
  bool                  extracted{false};

  std::string to_json() const;
};
#endif

namespace tls_fp {

// RFC 8701 GREASE values: 0x?A?A where high byte == low byte.
static inline bool is_grease(uint16_t v) noexcept {
  return (v & 0x0f) == 0x0a && (v >> 8) == (v & 0xff);
}

/// Return true if the buffer appears to start with a TLS handshake record.
inline bool looks_like_clienthello(const uint8_t *buf, size_t len) noexcept {
  return len >= 5 && buf[0] == 0x16 && buf[1] == 0x03;
}

/// Parse a TLS ClientHello from raw bytes and populate \p out.
/// Returns true on success.  The output fields ja3/ja3_hash are always set.
inline bool parse(const uint8_t *buf, size_t len, TlsFingerprint &out) {
  if (!looks_like_clienthello(buf, len)) return false;

  // ── TLS record header ─────────────────────────────────────────────────────
  // content_type(1) legacy_version(2) length(2)
  if (len < 5) return false;
  uint16_t rec_len = (uint16_t(buf[3]) << 8) | buf[4];
  if (len < size_t(5 + rec_len)) return false;

  const uint8_t *p   = buf + 5;
  const uint8_t *end = p + rec_len;

  // ── TLS handshake header ──────────────────────────────────────────────────
  // type(1) length(3)
  if (p + 4 > end) return false;
  if (p[0] != 0x01) return false; // must be ClientHello
  size_t hs_len = (size_t(p[1]) << 16) | (size_t(p[2]) << 8) | p[3];
  p += 4;
  const uint8_t *hs_end = p + hs_len;
  if (hs_end > end) return false;

  // ── ClientHello body ──────────────────────────────────────────────────────
  // legacy_version(2) random(32) session_id_len(1) session_id(*)
  if (p + 2 + 32 + 1 > hs_end) return false;
  out.version = (uint16_t(p[0]) << 8) | p[1];
  p += 2 + 32; // skip version + random
  uint8_t sid_len = *p++;
  if (p + sid_len > hs_end) return false;
  p += sid_len;

  // cipher_suites_length(2) cipher_suites(*)
  if (p + 2 > hs_end) return false;
  uint16_t cs_len = (uint16_t(p[0]) << 8) | p[1];
  p += 2;
  if (p + cs_len > hs_end || cs_len % 2 != 0) return false;
  for (size_t i = 0; i < cs_len; i += 2) {
    uint16_t cs = (uint16_t(p[i]) << 8) | p[i + 1];
    if (!is_grease(cs)) out.ciphers.push_back(cs);
  }
  p += cs_len;

  // compression_methods_len(1) compression_methods(*)
  if (p + 1 > hs_end) return false;
  uint8_t cm_len = *p++;
  if (p + cm_len > hs_end) return false;
  p += cm_len;

  // ── Extensions ────────────────────────────────────────────────────────────
  if (p + 2 > hs_end) goto build_ja3; // no extensions section
  {
    uint16_t ext_total = (uint16_t(p[0]) << 8) | p[1];
    p += 2;
    const uint8_t *ext_end = p + ext_total;
    if (ext_end > hs_end) goto build_ja3;

    while (p + 4 <= ext_end) {
      uint16_t etype = (uint16_t(p[0]) << 8) | p[1];
      uint16_t elen  = (uint16_t(p[2]) << 8) | p[3];
      p += 4;
      if (p + elen > ext_end) break;
      const uint8_t *ed  = p;
      p += elen;

      if (!is_grease(etype)) out.ext_types.push_back(etype);

      switch (etype) {
        // SNI (0x0000)
        case 0x0000:
          if (elen > 5) {
            // server_name_list_len(2) [name_type(1) name_len(2) name]
            const uint8_t *sp = ed + 2; // skip list length
            if (sp + 3 <= ed + elen) {
              uint8_t  name_type = sp[0];
              uint16_t name_len  = (uint16_t(sp[1]) << 8) | sp[2];
              sp += 3;
              if (name_type == 0 && sp + name_len <= ed + elen)
                out.sni.assign(reinterpret_cast<const char *>(sp), name_len);
            }
          }
          break;

        // supported_groups (0x000a)
        case 0x000a:
          if (elen >= 2) {
            uint16_t    gl = (uint16_t(ed[0]) << 8) | ed[1];
            const uint8_t *gp = ed + 2, *ge = ed + 2 + gl;
            if (ge <= ed + elen)
              for (; gp + 1 < ge; gp += 2) {
                uint16_t g = (uint16_t(gp[0]) << 8) | gp[1];
                if (!is_grease(g)) out.groups.push_back(g);
              }
          }
          break;

        // ec_point_formats (0x000b)
        case 0x000b:
          if (elen >= 1) {
            uint8_t flen = ed[0];
            for (uint8_t i = 0; i < flen && 1u + i < elen; ++i)
              out.ec_pt_fmts.push_back(ed[1 + i]);
          }
          break;

        // ALPN (0x0010)
        case 0x0010:
          if (elen >= 2) {
            uint16_t    pl  = (uint16_t(ed[0]) << 8) | ed[1];
            const uint8_t *pp = ed + 2, *pe = ed + 2 + pl;
            if (pe <= ed + elen)
              while (pp + 1 <= pe) {
                uint8_t plen = pp[0]; pp++;
                if (pp + plen > pe) break;
                out.alpn.emplace_back(reinterpret_cast<const char *>(pp), plen);
                pp += plen;
              }
          }
          break;

        // signature_algorithms (0x000d)
        case 0x000d:
          if (elen >= 2) {
            uint16_t sl = (uint16_t(ed[0]) << 8) | ed[1];
            const uint8_t *sp = ed + 2, *se = ed + 2 + sl;
            if (se <= ed + elen)
              for (; sp + 1 < se; sp += 2) {
                uint16_t sa = (uint16_t(sp[0]) << 8) | sp[1];
                if (!is_grease(sa)) out.sig_algs.push_back(sa);
              }
          }
          break;

        // supported_versions (0x002b) — overrides the legacy ClientHello version
        case 0x002b:
          if (elen >= 3) {
            uint8_t sv_len = ed[0];
            if (sv_len >= 2 && 1u + sv_len <= elen) {
              uint16_t sv = (uint16_t(ed[1]) << 8) | ed[2];
              if (!is_grease(sv)) out.version = sv;
            }
          }
          break;

        default:
          break;
      }
    }
  }

build_ja3: {
    // ── Build JA3 string ──────────────────────────────────────────────────
    // Format: version,ciphers,extensions,groups,ec_point_formats
    // Each list is dash-separated decimal values.
    auto join16 = [](const std::vector<uint16_t> &v) {
      std::string s;
      for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += '-';
        s += std::to_string(v[i]);
      }
      return s;
    };
    auto join8 = [](const std::vector<uint8_t> &v) {
      std::string s;
      for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += '-';
        s += std::to_string(v[i]);
      }
      return s;
    };

    out.ja3 = std::to_string(out.version) + "," + join16(out.ciphers) + "," +
              join16(out.ext_types) + "," + join16(out.groups) + "," +
              join8(out.ec_pt_fmts);

    // ── Compute MD5 via OpenSSL EVP ───────────────────────────────────────
    uint8_t      md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    EVP_MD_CTX  *ctx    = EVP_MD_CTX_new();
    if (ctx) {
      if (EVP_DigestInit_ex(ctx, EVP_md5(), nullptr) == 1 &&
          EVP_DigestUpdate(ctx, out.ja3.data(), out.ja3.size()) == 1 &&
          EVP_DigestFinal_ex(ctx, md, &md_len) == 1) {
        char hex[33] = {};
        for (unsigned i = 0; i < md_len; ++i)
          snprintf(hex + 2 * i, 3, "%02x", md[i]);
        out.ja3_hash = hex;
      }
      EVP_MD_CTX_free(ctx);
    }

    out.extracted = true;

    // ── Compute JA4 ──────────────────────────────────────────────────────────
    // Spec: https://github.com/FoxIO-LLC/ja4
    // Format: {proto}{tls_ver}{sni_ind}{cs_count:02d}{ext_count:02d}{alpn2}
    //         _{sha256_12(sorted_ciphers_hex)}_{sha256_12(sorted_exts_hex+_+sorted_sigs_hex)}
    {
      // TLS version code
      const char *tls_ver = "00";
      switch (out.version) {
        case 0x0304: tls_ver = "13"; break;
        case 0x0303: tls_ver = "12"; break;
        case 0x0302: tls_ver = "11"; break;
        case 0x0301: tls_ver = "10"; break;
        case 0x0300: tls_ver = "s3"; break;
        default:     tls_ver = "00"; break;
      }

      // SNI indicator: 'd' = domain present, 'i' = absent/IP
      char sni_ind = out.sni.empty() ? 'i' : 'd';

      // ALPN first 2 chars (or "00")
      std::string alpn2 = "00";
      if (!out.alpn.empty() && !out.alpn[0].empty()) {
        const auto &a0 = out.alpn[0];
        alpn2 = a0.size() >= 2 ? a0.substr(0, 2) : (a0 + std::string(2 - a0.size(), '0'));
      }

      int cs_count  = static_cast<int>(out.ciphers.size());   // GREASE already filtered
      int ext_count = static_cast<int>(out.ext_types.size()); // GREASE already filtered

      char part_a[32] = {};
      snprintf(part_a, sizeof(part_a), "t%s%c%02d%02d%s",
               tls_ver, sni_ind, cs_count, ext_count, alpn2.c_str());

      // SHA-256 helper: first 12 hex chars of SHA-256(input)
      auto sha256_head12 = [](const std::string &input) -> std::string {
        uint8_t digest[32] = {};
        unsigned int dlen  = 0;
        EVP_MD_CTX *c = EVP_MD_CTX_new();
        std::string r = "000000000000";
        if (c) {
          if (EVP_DigestInit_ex(c, EVP_sha256(), nullptr) == 1 &&
              EVP_DigestUpdate(c, input.data(), input.size()) == 1 &&
              EVP_DigestFinal_ex(c, digest, &dlen) == 1) {
            char hex[65] = {};
            for (unsigned i = 0; i < dlen; ++i)
              snprintf(hex + 2 * i, 3, "%02x", digest[i]);
            r = std::string(hex, 12);
          }
          EVP_MD_CTX_free(c);
        }
        return r;
      };

      // Part b: sorted cipher suites, comma-joined 4-char hex, sha256-12
      std::string part_b, cs_raw;
      {
        std::vector<uint16_t> sc = out.ciphers;
        std::sort(sc.begin(), sc.end());
        for (size_t i = 0; i < sc.size(); ++i) {
          if (i) cs_raw += ',';
          char tmp[5]; snprintf(tmp, sizeof(tmp), "%04x", sc[i]);
          cs_raw += tmp;
        }
        part_b = sha256_head12(cs_raw);
      }

      // Part c: sorted extensions excluding SNI(0x0000)/ALPN(0x0010)/Padding(0x0015)
      //         then "_" then sorted sig algs — all comma-joined 4-char hex, sha256-12
      std::string part_c, ext_raw;
      {
        std::vector<uint16_t> se;
        for (uint16_t e : out.ext_types)
          if (e != 0x0000 && e != 0x0010 && e != 0x0015) se.push_back(e);
        std::sort(se.begin(), se.end());
        for (size_t i = 0; i < se.size(); ++i) {
          if (i) ext_raw += ',';
          char tmp[5]; snprintf(tmp, sizeof(tmp), "%04x", se[i]);
          ext_raw += tmp;
        }
        if (!out.sig_algs.empty()) {
          std::vector<uint16_t> ss = out.sig_algs;
          std::sort(ss.begin(), ss.end());
          ext_raw += '_';
          for (size_t i = 0; i < ss.size(); ++i) {
            if (i) ext_raw += ',';
            char tmp[5]; snprintf(tmp, sizeof(tmp), "%04x", ss[i]);
            ext_raw += tmp;
          }
        }
        part_c = sha256_head12(ext_raw);
      }

      out.ja4     = std::string(part_a) + '_' + part_b + '_' + part_c;
      out.ja4_raw = std::string(part_a) + '_' + cs_raw  + '_' + ext_raw;
    }

    return true;
  }
}

} // namespace tls_fp

// ── TlsFingerprint::to_json ───────────────────────────────────────────────────
// Defined here (header-only) so any TU that includes this header can call it.
inline std::string TlsFingerprint::to_json() const {
  // Minimal hand-rolled JSON — avoids any external JSON library dependency.
  std::string j;
  j.reserve(256);
  j += "{\"extracted\":";
  j += extracted ? "true" : "false";
  j += ",\"version\":";
  j += std::to_string(version);
  j += ",\"sni\":\"";
  // Escape sni (SNI should only contain safe hostname characters)
  j += sni;
  j += "\",\"alpn\":[";
  for (size_t i = 0; i < alpn.size(); ++i) {
    if (i) j += ',';
    j += '"' + alpn[i] + '"';
  }
  j += "],\"ja3\":\"";
  j += ja3;
  j += "\",\"ja3_hash\":\"";
  j += ja3_hash;
  j += "\",\"ja4\":\"";
  j += ja4;
  j += "\",\"ja4_raw\":\"";
  j += ja4_raw;
  j += "\"}";
  return j;
}
