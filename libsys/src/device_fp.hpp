// device_fp.hpp ─ internal helper for GetDeviceFingerprint()
// Portable SHA-256 + HMAC-SHA256, no external dependencies.
// All symbols are in an anonymous namespace to avoid ODR issues.
#pragma once
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace device_fp {

// ─────────────────────────────────────────────────────────────────────────────
// SHA-256  (FIPS 180-4)
// ─────────────────────────────────────────────────────────────────────────────
static const uint32_t SHA256_K[64] = {
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
    0x90beffafu, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

struct Sha256Ctx {
  uint32_t h[8];
  uint64_t bitlen; // total bits processed
  uint8_t  buf[64];
  uint32_t buflen;
};

inline uint32_t rotr32(uint32_t x, int n) {
  return (x >> n) | (x << (32 - n));
}

static void sha256_transform(uint32_t h[8], const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = ((uint32_t)block[i * 4 + 0] << 24) |
           ((uint32_t)block[i * 4 + 1] << 16) |
           ((uint32_t)block[i * 4 + 2] << 8) |
           ((uint32_t)block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
  uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; ++i) {
    uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t tmp1 = hh + S1 + ch + SHA256_K[i] + w[i];
    uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t tmp2 = S0 + maj;
    hh = g; g = f; f = e; e = d + tmp1;
    d = c; c = b; b = a; a = tmp1 + tmp2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d;
  h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static void sha256_init(Sha256Ctx& ctx) {
  ctx.h[0]=0x6a09e667u; ctx.h[1]=0xbb67ae85u;
  ctx.h[2]=0x3c6ef372u; ctx.h[3]=0xa54ff53au;
  ctx.h[4]=0x510e527fu; ctx.h[5]=0x9b05688cu;
  ctx.h[6]=0x1f83d9abu; ctx.h[7]=0x5be0cd19u;
  ctx.bitlen=0; ctx.buflen=0;
}

static void sha256_update(Sha256Ctx& ctx, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    ctx.buf[ctx.buflen++] = data[i];
    if (ctx.buflen == 64) {
      sha256_transform(ctx.h, ctx.buf);
      ctx.buflen = 0;
    }
  }
  ctx.bitlen += (uint64_t)len * 8u;
}

static void sha256_final(Sha256Ctx& ctx, uint8_t out[32]) {
  uint32_t i = ctx.buflen;
  ctx.buf[i++] = 0x80;
  if (i > 56) {
    while (i < 64) ctx.buf[i++] = 0;
    sha256_transform(ctx.h, ctx.buf);
    i = 0;
  }
  while (i < 56) ctx.buf[i++] = 0;
  // big-endian 64-bit bit length
  uint64_t bl = ctx.bitlen;
  ctx.buf[56]=(uint8_t)(bl>>56); ctx.buf[57]=(uint8_t)(bl>>48);
  ctx.buf[58]=(uint8_t)(bl>>40); ctx.buf[59]=(uint8_t)(bl>>32);
  ctx.buf[60]=(uint8_t)(bl>>24); ctx.buf[61]=(uint8_t)(bl>>16);
  ctx.buf[62]=(uint8_t)(bl>> 8); ctx.buf[63]=(uint8_t)(bl    );
  sha256_transform(ctx.h, ctx.buf);
  for (int j = 0; j < 8; ++j) {
    out[j*4+0] = (uint8_t)(ctx.h[j] >> 24);
    out[j*4+1] = (uint8_t)(ctx.h[j] >> 16);
    out[j*4+2] = (uint8_t)(ctx.h[j] >>  8);
    out[j*4+3] = (uint8_t)(ctx.h[j]      );
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// HMAC-SHA256  (RFC 2104)
// ─────────────────────────────────────────────────────────────────────────────
static void hmac_sha256(const uint8_t* key, size_t klen,
                        const uint8_t* msg, size_t mlen,
                        uint8_t out[32]) {
  uint8_t k[64] = {};
  if (klen > 64) {
    // hash long key
    Sha256Ctx cx; sha256_init(cx);
    sha256_update(cx, key, klen);
    sha256_final(cx, k);
  } else {
    std::memcpy(k, key, klen);
  }
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; ++i) {
    ipad[i] = k[i] ^ 0x36u;
    opad[i] = k[i] ^ 0x5cu;
  }
  uint8_t inner[32];
  Sha256Ctx cx;
  sha256_init(cx);
  sha256_update(cx, ipad, 64);
  sha256_update(cx, msg, mlen);
  sha256_final(cx, inner);

  sha256_init(cx);
  sha256_update(cx, opad, 64);
  sha256_update(cx, inner, 32);
  sha256_final(cx, out);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public helper
// ─────────────────────────────────────────────────────────────────────────────

// Fixed application key – change version suffix if the algorithm changes.
static const char APP_KEY[] = "brosdk-device-fp-v1";

// Join collected hardware parts with '|' and return HMAC-SHA256 hex string.
// Parts that failed to collect should be pushed as empty strings so that the
// slot count is stable (avoids accidental collisions across platforms).
static std::string make_fingerprint(const std::vector<std::string>& parts) {
  std::string joined;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) joined += '|';
    joined += parts[i];
  }
  uint8_t mac[32];
  hmac_sha256(reinterpret_cast<const uint8_t*>(APP_KEY), sizeof(APP_KEY) - 1,
              reinterpret_cast<const uint8_t*>(joined.data()), joined.size(),
              mac);
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (int i = 0; i < 32; ++i)
    oss << std::setw(2) << (unsigned)mac[i];
  return oss.str();
}

} // namespace device_fp
} // anonymous namespace
