// net_env_impl.hpp – shared JA3/JA4 computation helpers.
// Included ONLY by platform-specific net.cc files – not a public header.
#pragma once
#include <libsys.h>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// Minimal RFC-1321 MD5 (used for JA3 fingerprint)
// ─────────────────────────────────────────────────────────────────────────────
namespace nenv_detail {

struct MD5Ctx {
  uint32_t state[4];
  uint32_t count[2];
  uint8_t buf[64];
};

static const uint8_t kMD5Padding[64] = {0x80};

static void md5_init(MD5Ctx &c) {
  c.count[0] = c.count[1] = 0;
  c.state[0] = 0x67452301u;
  c.state[1] = 0xefcdab89u;
  c.state[2] = 0x98badcfeu;
  c.state[3] = 0x10325476u;
}

#define MD5_F(x, y, z) ((x & y) | (~x & z))
#define MD5_G(x, y, z) ((x & z) | (y & ~z))
#define MD5_H(x, y, z) (x ^ y ^ z)
#define MD5_I(x, y, z) (y ^ (x | ~z))
#define MD5_ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define MD5_FF(a, b, c, d, x, s, t)                                            \
  a = b + MD5_ROL(a + MD5_F(b, c, d) + x + t, s)
#define MD5_GG(a, b, c, d, x, s, t)                                            \
  a = b + MD5_ROL(a + MD5_G(b, c, d) + x + t, s)
#define MD5_HH(a, b, c, d, x, s, t)                                            \
  a = b + MD5_ROL(a + MD5_H(b, c, d) + x + t, s)
#define MD5_II(a, b, c, d, x, s, t)                                            \
  a = b + MD5_ROL(a + MD5_I(b, c, d) + x + t, s)

static void md5_transform(uint32_t state[4], const uint8_t block[64]) {
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t x[16];
  for (int i = 0; i < 16; i++) {
    x[i] = ((uint32_t)block[i * 4]) | ((uint32_t)block[i * 4 + 1] << 8) |
           ((uint32_t)block[i * 4 + 2] << 16) |
           ((uint32_t)block[i * 4 + 3] << 24);
  }
  MD5_FF(a, b, c, d, x[0], 7, 0xd76aa478u);
  MD5_FF(d, a, b, c, x[1], 12, 0xe8c7b756u);
  MD5_FF(c, d, a, b, x[2], 17, 0x242070dbu);
  MD5_FF(b, c, d, a, x[3], 22, 0xc1bdceeeu);
  MD5_FF(a, b, c, d, x[4], 7, 0xf57c0fafu);
  MD5_FF(d, a, b, c, x[5], 12, 0x4787c62au);
  MD5_FF(c, d, a, b, x[6], 17, 0xa8304613u);
  MD5_FF(b, c, d, a, x[7], 22, 0xfd469501u);
  MD5_FF(a, b, c, d, x[8], 7, 0x698098d8u);
  MD5_FF(d, a, b, c, x[9], 12, 0x8b44f7afu);
  MD5_FF(c, d, a, b, x[10], 17, 0xffff5bb1u);
  MD5_FF(b, c, d, a, x[11], 22, 0x895cd7beu);
  MD5_FF(a, b, c, d, x[12], 7, 0x6b901122u);
  MD5_FF(d, a, b, c, x[13], 12, 0xfd987193u);
  MD5_FF(c, d, a, b, x[14], 17, 0xa679438eu);
  MD5_FF(b, c, d, a, x[15], 22, 0x49b40821u);

  MD5_GG(a, b, c, d, x[1], 5, 0xf61e2562u);
  MD5_GG(d, a, b, c, x[6], 9, 0xc040b340u);
  MD5_GG(c, d, a, b, x[11], 14, 0x265e5a51u);
  MD5_GG(b, c, d, a, x[0], 20, 0xe9b6c7aau);
  MD5_GG(a, b, c, d, x[5], 5, 0xd62f105du);
  MD5_GG(d, a, b, c, x[10], 9, 0x02441453u);
  MD5_GG(c, d, a, b, x[15], 14, 0xd8a1e681u);
  MD5_GG(b, c, d, a, x[4], 20, 0xe7d3fbc8u);
  MD5_GG(a, b, c, d, x[9], 5, 0x21e1cde6u);
  MD5_GG(d, a, b, c, x[14], 9, 0xc33707d6u);
  MD5_GG(c, d, a, b, x[3], 14, 0xf4d50d87u);
  MD5_GG(b, c, d, a, x[8], 20, 0x455a14edu);
  MD5_GG(a, b, c, d, x[13], 5, 0xa9e3e905u);
  MD5_GG(d, a, b, c, x[2], 9, 0xfcefa3f8u);
  MD5_GG(c, d, a, b, x[7], 14, 0x676f02d9u);
  MD5_GG(b, c, d, a, x[12], 20, 0x8d2a4c8au);

  MD5_HH(a, b, c, d, x[5], 4, 0xfffa3942u);
  MD5_HH(d, a, b, c, x[8], 11, 0x8771f681u);
  MD5_HH(c, d, a, b, x[11], 16, 0x6d9d6122u);
  MD5_HH(b, c, d, a, x[14], 23, 0xfde5380cu);
  MD5_HH(a, b, c, d, x[1], 4, 0xa4beea44u);
  MD5_HH(d, a, b, c, x[4], 11, 0x4bdecfa9u);
  MD5_HH(c, d, a, b, x[7], 16, 0xf6bb4b60u);
  MD5_HH(b, c, d, a, x[10], 23, 0xbebfbc70u);
  MD5_HH(a, b, c, d, x[13], 4, 0x289b7ec6u);
  MD5_HH(d, a, b, c, x[0], 11, 0xeaa127fau);
  MD5_HH(c, d, a, b, x[3], 16, 0xd4ef3085u);
  MD5_HH(b, c, d, a, x[6], 23, 0x04881d05u);
  MD5_HH(a, b, c, d, x[9], 4, 0xd9d4d039u);
  MD5_HH(d, a, b, c, x[12], 11, 0xe6db99e5u);
  MD5_HH(c, d, a, b, x[15], 16, 0x1fa27cf8u);
  MD5_HH(b, c, d, a, x[2], 23, 0xc4ac5665u);

  MD5_II(a, b, c, d, x[0], 6, 0xf4292244u);
  MD5_II(d, a, b, c, x[7], 10, 0x432aff97u);
  MD5_II(c, d, a, b, x[14], 15, 0xab9423a7u);
  MD5_II(b, c, d, a, x[5], 21, 0xfc93a039u);
  MD5_II(a, b, c, d, x[12], 6, 0x655b59c3u);
  MD5_II(d, a, b, c, x[3], 10, 0x8f0ccc92u);
  MD5_II(c, d, a, b, x[10], 15, 0xffeff47du);
  MD5_II(b, c, d, a, x[1], 21, 0x85845dd1u);
  MD5_II(a, b, c, d, x[8], 6, 0x6fa87e4fu);
  MD5_II(d, a, b, c, x[15], 10, 0xfe2ce6e0u);
  MD5_II(c, d, a, b, x[6], 15, 0xa3014314u);
  MD5_II(b, c, d, a, x[13], 21, 0x4e0811a1u);
  MD5_II(a, b, c, d, x[4], 6, 0xf7537e82u);
  MD5_II(d, a, b, c, x[11], 10, 0xbd3af235u);
  MD5_II(c, d, a, b, x[2], 15, 0x2ad7d2bbu);
  MD5_II(b, c, d, a, x[9], 21, 0xeb86d391u);
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

static void md5_update(MD5Ctx &c, const uint8_t *in, uint32_t len) {
  uint32_t idx = (c.count[0] >> 3) & 0x3F;
  c.count[0] += (len << 3);
  if (c.count[0] < (len << 3))
    c.count[1]++;
  c.count[1] += (len >> 29);
  uint32_t part = 64 - idx;
  uint32_t i = 0;
  if (len >= part) {
    std::memcpy(&c.buf[idx], in, part);
    md5_transform(c.state, c.buf);
    for (i = part; i + 63 < len; i += 64)
      md5_transform(c.state, in + i);
    idx = 0;
  }
  std::memcpy(&c.buf[idx], in + i, len - i);
}

static void md5_final(MD5Ctx &c, uint8_t digest[16]) {
  uint8_t bits[8];
  for (int i = 0; i < 4; i++) {
    bits[i] = (c.count[0] >> (i * 8)) & 0xFF;
    bits[i + 4] = (c.count[1] >> (i * 8)) & 0xFF;
  }
  uint32_t idx = (c.count[0] >> 3) & 0x3F;
  uint32_t pad = (idx < 56) ? (56 - idx) : (120 - idx);
  md5_update(c, kMD5Padding, pad);
  md5_update(c, bits, 8);
  for (int i = 0; i < 4; i++) {
    digest[i * 4] = (c.state[i]) & 0xFF;
    digest[i * 4 + 1] = (c.state[i] >> 8) & 0xFF;
    digest[i * 4 + 2] = (c.state[i] >> 16) & 0xFF;
    digest[i * 4 + 3] = (c.state[i] >> 24) & 0xFF;
  }
}

static std::string tiny_md5(const std::string &input) {
  MD5Ctx c;
  md5_init(c);
  md5_update(c, reinterpret_cast<const uint8_t *>(input.data()),
             static_cast<uint32_t>(input.size()));
  uint8_t d[16];
  md5_final(c, d);
  char hex[33];
  for (int i = 0; i < 16; i++)
    std::snprintf(hex + i * 2, 3, "%02x", d[i]);
  hex[32] = '\0';
  return std::string(hex, 32);
}

// ─────────────────────────────────────────────────────────────────────────────
// Minimal FIPS-180-4 SHA-256 (used for JA4 fingerprint)
// ─────────────────────────────────────────────────────────────────────────────
static const uint32_t kSHA256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

#define SHA256_ROR(x, n) ((x >> n) | (x << (32 - n)))
#define SHA256_CH(e, f, g) ((e & f) ^ (~e & g))
#define SHA256_MAJ(a, b, c) ((a & b) ^ (a & c) ^ (b & c))
#define SHA256_S0(a) (SHA256_ROR(a, 2) ^ SHA256_ROR(a, 13) ^ SHA256_ROR(a, 22))
#define SHA256_S1(e) (SHA256_ROR(e, 6) ^ SHA256_ROR(e, 11) ^ SHA256_ROR(e, 25))
#define SHA256_s0(x) (SHA256_ROR(x, 7) ^ SHA256_ROR(x, 18) ^ (x >> 3))
#define SHA256_s1(x) (SHA256_ROR(x, 17) ^ SHA256_ROR(x, 19) ^ (x >> 10))

static std::string tiny_sha256(const std::string &input) {
  uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  const uint8_t *msg = reinterpret_cast<const uint8_t *>(input.data());
  uint64_t len = input.size();

  // pre-processing: build padded message
  uint64_t bitlen = len * 8;
  size_t plen = len + 1;
  while (plen % 64 != 56)
    plen++;
  plen += 8; // for length
  std::vector<uint8_t> padded(plen, 0);
  std::memcpy(padded.data(), msg, len);
  padded[len] = 0x80;
  for (int i = 7; i >= 0; i--)
    padded[plen - 8 + (7 - i)] = (bitlen >> (i * 8)) & 0xFF;

  for (size_t chunk = 0; chunk < plen; chunk += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
      w[i] = ((uint32_t)padded[chunk + i * 4] << 24) |
             ((uint32_t)padded[chunk + i * 4 + 1] << 16) |
             ((uint32_t)padded[chunk + i * 4 + 2] << 8) |
             ((uint32_t)padded[chunk + i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++)
      w[i] = SHA256_s1(w[i - 2]) + w[i - 7] + SHA256_s0(w[i - 15]) + w[i - 16];

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5],
             g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t t1 = hh + SHA256_S1(e) + SHA256_CH(e, f, g) + kSHA256K[i] + w[i];
      uint32_t t2 = SHA256_S0(a) + SHA256_MAJ(a, b, c);
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }
  char hex[65];
  for (int i = 0; i < 8; i++)
    std::snprintf(hex + i * 8, 9, "%08x", h[i]);
  hex[64] = '\0';
  return std::string(hex, 64);
}

// ─────────────────────────────────────────────────────────────────────────────
// JA3 / JA4 helpers
// ─────────────────────────────────────────────────────────────────────────────

// RFC 8701 GREASE values: 0x0A0A, 0x1A1A, …, 0xFAFA
inline bool is_grease(uint16_t v) {
  uint8_t lo = v & 0xFF, hi = (v >> 8) & 0xFF;
  return lo == hi && (lo & 0x0F) == 0x0A;
}

inline std::vector<uint16_t> filter_grease16(const std::vector<uint16_t> &in) {
  std::vector<uint16_t> out;
  out.reserve(in.size());
  for (auto v : in)
    if (!is_grease(v))
      out.push_back(v);
  return out;
}

inline std::string join_dash(const std::vector<uint16_t> &v) {
  std::string r;
  for (size_t i = 0; i < v.size(); i++) {
    if (i)
      r += '-';
    r += std::to_string((unsigned)v[i]);
  }
  return r;
}

inline std::string join_dash8(const std::vector<uint8_t> &v) {
  std::string r;
  for (size_t i = 0; i < v.size(); i++) {
    if (i)
      r += '-';
    r += std::to_string((unsigned)v[i]);
  }
  return r;
}

inline std::string join_comma_hex4(const std::vector<uint16_t> &v) {
  std::string r;
  char buf[8];
  for (size_t i = 0; i < v.size(); i++) {
    if (i)
      r += ',';
    std::snprintf(buf, sizeof(buf), "%04x", (unsigned)v[i]);
    r += buf;
  }
  return r;
}

inline std::string join_comma_dec(const std::vector<uint16_t> &v) {
  std::string r;
  for (size_t i = 0; i < v.size(); i++) {
    if (i)
      r += ',';
    r += std::to_string((unsigned)v[i]);
  }
  return r;
}

} // namespace nenv_detail

// Compute ja3_str, ja3 (MD5) and ja4 fields in tls_fp.
// Call AFTER filling cipher_suites, extensions, groups, ec_point_fmts, alpn,
// tls_version_max, tls_version_str.
inline void compute_tls_fingerprints(ISystem::NetEnvInfo::TlsFpInfo &fp) {
  using namespace nenv_detail;

  // Filter GREASE from all lists
  auto ciphers = filter_grease16(fp.cipher_suites);
  auto exts = filter_grease16(fp.extensions);
  auto groups = filter_grease16(fp.groups);

  // ── JA3 ──────────────────────────────────────────────────────────────
  // Record-layer version is always 0x0303 (771) for TLS 1.2+ clients
  // even when negotiating TLS 1.3 (backward compat per RFC 8446 §4.1.2).
  const uint16_t ja3_record_ver = 771;

  fp.ja3_str = std::to_string(ja3_record_ver) + "," + join_dash(ciphers) + "," +
               join_dash(exts) + "," + join_dash(groups) + "," +
               join_dash8(fp.ec_point_fmts);

  fp.ja3 = tiny_md5(fp.ja3_str);

  // ── JA4 ──────────────────────────────────────────────────────────────
  // Per FoxIO JA4 spec (https://github.com/FoxIO-LLC/ja4)
  // Format: t<tlsver><sni><##cs><##ex><alpn>_<cs_hash>_<ext_hash>

  // TLS version label (highest supported)
  const char *tls_v = "13";
  if (fp.tls_version_max >= 0x0304)
    tls_v = "13";
  else if (fp.tls_version_max >= 0x0303)
    tls_v = "12";
  else if (fp.tls_version_max >= 0x0302)
    tls_v = "11";
  else if (fp.tls_version_max >= 0x0301)
    tls_v = "10";
  else
    tls_v = "s3";

  // SNI is present in standard modern clients → 'd'
  const char sni_c = 'd';

  // Count ciphers (exclude GREASE and TLS_EMPTY_RENEGOTIATION_INFO_SCSV)
  std::vector<uint16_t> ja4_cs;
  for (auto c : ciphers)
    if (c != 0x00FF)
      ja4_cs.push_back(c);

  // Extension count (all after GREASE filter)
  int num_cs = (int)ja4_cs.size();
  int num_ex = (int)exts.size();

  // ALPN: first 2 chars of first ALPN protocol (or "00")
  std::string alpn_tag = "00";
  if (!fp.alpn.empty()) {
    const auto &a = fp.alpn.front();
    if (a.size() >= 2)
      alpn_tag = a.substr(0, 2);
    else if (a.size() == 1)
      alpn_tag = a + "0";
  }

  // Part A
  char part_a[32];
  std::snprintf(part_a, sizeof(part_a), "t%s%c%02d%02d%s", tls_v, sni_c,
                num_cs > 99 ? 99 : num_cs, num_ex > 99 ? 99 : num_ex,
                alpn_tag.c_str());

  // Part B: SHA256[0:12] of cipher suites sorted ascending, in hex,
  // comma-joined
  std::vector<uint16_t> sorted_cs = ja4_cs;
  std::sort(sorted_cs.begin(), sorted_cs.end());
  std::string cs_hash = tiny_sha256(join_comma_hex4(sorted_cs)).substr(0, 12);

  // Part C: SHA256[0:12] of extensions in-order (excluding SNI=0 and ALPN=16)
  std::vector<uint16_t> ex_for_hash;
  for (auto e : exts)
    if (e != 0 && e != 16)
      ex_for_hash.push_back(e);
  std::string ex_hash = tiny_sha256(join_comma_dec(ex_for_hash)).substr(0, 12);

  fp.ja4 = std::string(part_a) + "_" + cs_hash + "_" + ex_hash;
}

// Human-readable TLS version string from a 2-byte version code
inline const char *tls_version_str(uint16_t v) {
  switch (v) {
  case 0x0304:
    return "TLS 1.3";
  case 0x0303:
    return "TLS 1.2";
  case 0x0302:
    return "TLS 1.1";
  case 0x0301:
    return "TLS 1.0";
  case 0x0300:
    return "SSL 3.0";
  default:
    return "Unknown";
  }
}

// Name → IANA cipher suite code lookup (Windows SChannel registry names →
// codes)
inline uint16_t cipher_name_to_iana(const std::string &name) {
  static const struct {
    const char *n;
    uint16_t v;
  } tbl[] = {
      // TLS 1.3
      {"TLS_AES_256_GCM_SHA384", 0x1302},
      {"TLS_AES_128_GCM_SHA256", 0x1301},
      {"TLS_CHACHA20_POLY1305_SHA256", 0x1303},
      // ECDHE-ECDSA
      {"TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384", 0xC02C},
      {"TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256", 0xC02B},
      {"TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256", 0xCCA9},
      {"TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384", 0xC024},
      {"TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256", 0xC023},
      {"TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA", 0xC00A},
      {"TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA", 0xC009},
      {"TLS_ECDHE_ECDSA_WITH_3DES_EDE_CBC_SHA", 0xC008},
      // ECDHE-RSA
      {"TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384", 0xC030},
      {"TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256", 0xC02F},
      {"TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256", 0xCCA8},
      {"TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384", 0xC028},
      {"TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256", 0xC027},
      {"TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA", 0xC014},
      {"TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA", 0xC013},
      {"TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA", 0xC012},
      // DHE-RSA
      {"TLS_DHE_RSA_WITH_AES_256_GCM_SHA384", 0x009F},
      {"TLS_DHE_RSA_WITH_AES_128_GCM_SHA256", 0x009E},
      {"TLS_DHE_RSA_WITH_CHACHA20_POLY1305_SHA256", 0xCCAA},
      {"TLS_DHE_RSA_WITH_AES_256_CBC_SHA256", 0x006B},
      {"TLS_DHE_RSA_WITH_AES_128_CBC_SHA256", 0x0067},
      {"TLS_DHE_RSA_WITH_AES_256_CBC_SHA", 0x0039},
      {"TLS_DHE_RSA_WITH_AES_128_CBC_SHA", 0x0033},
      {"TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA", 0x0016},
      // RSA
      {"TLS_RSA_WITH_AES_256_GCM_SHA384", 0x009D},
      {"TLS_RSA_WITH_AES_128_GCM_SHA256", 0x009C},
      {"TLS_RSA_WITH_AES_256_CBC_SHA256", 0x003D},
      {"TLS_RSA_WITH_AES_128_CBC_SHA256", 0x003C},
      {"TLS_RSA_WITH_AES_256_CBC_SHA", 0x0035},
      {"TLS_RSA_WITH_AES_128_CBC_SHA", 0x002F},
      {"TLS_RSA_WITH_3DES_EDE_CBC_SHA", 0x000A},
      {"TLS_RSA_WITH_RC4_128_SHA", 0x0005},
      {"TLS_RSA_WITH_RC4_128_MD5", 0x0004},
      {"TLS_RSA_WITH_NULL_SHA256", 0x003B},
      {"TLS_RSA_WITH_NULL_SHA", 0x0002},
  };
  for (const auto &e : tbl)
    if (name == e.n)
      return e.v;
  return 0; // unknown
}
