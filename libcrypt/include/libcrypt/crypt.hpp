#pragma once
#include "crypt.h"
#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace devkit {
/* Header-only C++20 convenience layer. All C++ allocation stays in the
 * consumer; only C ABI functions cross the library boundary. No business
 * formats here. */
class Crypt {
  static dkcrypt_span view(std::span<const uint8_t> s) {
    return {s.data(), s.size()};
  }
  template <class T, class F> static bool array(T &out, F fn) {
    dkcrypt_buffer b{out.data(), out.size(), 0};
    if (fn(&b) == DKCRYPT_OK && b.size == out.size())
      return true;
    SecureZero(out);
    return false;
  }
  template <class F>
  static bool bytes(size_t capacity, std::vector<uint8_t> &out, F fn) {
    SecureZero(out);
    out.clear();
    if (capacity > ((size_t(DKCRYPT_MAX_INPUT) + 2) / 3) * 4 + 128)
      return false;
    out.resize(capacity);
    dkcrypt_buffer b{out.data(), out.size(), 0};
    if (fn(&b) != DKCRYPT_OK) {
      SecureZero(out);
      out.clear();
      return false;
    }
    out.resize(b.size);
    return true;
  }

public:
  using Bytes = std::vector<uint8_t>;
  using Hash256 = std::array<uint8_t, 32>;
  using Hash512 = std::array<uint8_t, 64>;
  using Hash128 = std::array<uint8_t, 16>;
  using HmacSha256Key = Hash256;
  using Ed25519Seed = Hash256;
  using Ed25519PublicKey = Hash256;
  using Ed25519SecretKey = Hash512;
  using Ed25519Signature = Hash512;
  using X25519PublicKey = Hash256;
  using X25519SecretKey = Hash256;
  using X25519SharedSecret = Hash256;
  using XChaCha20Poly1305Key = Hash256;
  using XChaCha20Poly1305Nonce = std::array<uint8_t, 24>;
  using PasswordHashSalt = Hash128;
  static constexpr size_t kXChaCha20Poly1305TagSize = 16;
  struct Ed25519KeyPair {
    Ed25519PublicKey public_key{};
    Ed25519SecretKey secret_key{};
  };
  struct X25519KeyPair {
    X25519PublicKey public_key{};
    X25519SecretKey secret_key{};
  };
  class SecretKey {
  public:
    Ed25519SecretKey bytes{};
    SecretKey() = default;
    SecretKey(const SecretKey &) = delete;
    SecretKey &operator=(const SecretKey &) = delete;
    ~SecretKey() { SecureZero(bytes); }
  };
  static bool Initialize() noexcept {
    return dkcrypt_initialize(DKCRYPT_ABI_VERSION) == DKCRYPT_OK;
  }
  static void SecureZero(std::span<uint8_t> b) noexcept {
    dkcrypt_secure_zero(b.data(), b.size());
  }
  static bool ConstantTimeEqual(std::span<const uint8_t> a,
                                std::span<const uint8_t> b) noexcept {
    int32_t result = 0;
    return dkcrypt_equal(view(a), view(b), &result) == DKCRYPT_OK && result;
  }
  static bool RandomFill(std::span<uint8_t> out) noexcept {
    dkcrypt_buffer b{out.data(), out.size(), 0};
    return dkcrypt_random(&b, out.size()) == DKCRYPT_OK;
  }
  static bool RandomBytes(size_t size, Bytes &out) {
    return bytes(size, out, [&](auto *b) { return dkcrypt_random(b, size); });
  }
  static bool Sha256(std::span<const uint8_t> in, Hash256 &out) {
    return array(out, [&](auto *b) {
      return dkcrypt_hash(DKCRYPT_SHA256, view(in), b);
    });
  }
  static bool Sha512(std::span<const uint8_t> in, Hash512 &out) {
    return array(out, [&](auto *b) {
      return dkcrypt_hash(DKCRYPT_SHA512, view(in), b);
    });
  }
  static bool Blake2b256(std::span<const uint8_t> in, Hash256 &out) {
    return array(out, [&](auto *b) {
      return dkcrypt_hash(DKCRYPT_BLAKE2B256, view(in), b);
    });
  }
  static bool Md5(std::span<const uint8_t> in, Hash128 &out) {
    return array(out, [&](auto *b) {
      return dkcrypt_hash(DKCRYPT_MD5_LEGACY, view(in), b);
    });
  }
  static bool HmacSha256(std::span<const uint8_t> in, const HmacSha256Key &key,
                         Hash256 &out) {
    return array(out, [&](auto *b) {
      return dkcrypt_hmac_sha256(view(key), view(in), b);
    });
  }
  static bool DeriveSubkey(const Hash256 &key, std::span<const uint8_t> context,
                           uint64_t id, Hash256 &out) {
    return array(out, [&](auto *b) {
      return dkcrypt_kdf_blake2b(view(key), view(context), id, b);
    });
  }
  static bool Argon2id(std::string_view password, const PasswordHashSalt &salt,
                       uint64_t operations, size_t memory, Hash256 &out) {
    return array(out, [&](auto *b) {
      return dkcrypt_argon2id(
          {reinterpret_cast<const uint8_t *>(password.data()), password.size()},
          view(salt), operations, memory, b);
    });
  }
  static bool Base64Encode(std::span<const uint8_t> in, std::string &out) {
    if (in.size() > DKCRYPT_MAX_INPUT) {
      out.clear();
      return false;
    }
    Bytes b;
    bool ok = bytes(((in.size() + 2) / 3) * 4, b, [&](auto *r) {
      return dkcrypt_encode(DKCRYPT_BASE64, view(in), r);
    });
    out.assign(b.begin(), b.end());
    SecureZero(b);
    return ok;
  }
  static bool Base64Decode(const std::string &in, Bytes &out) {
    return bytes(in.size(), out, [&](auto *b) {
      return dkcrypt_decode(
          DKCRYPT_BASE64,
          {reinterpret_cast<const uint8_t *>(in.data()), in.size()}, b);
    });
  }
  static std::string Base58Encode(std::span<const uint8_t> in) {
    if (in.size() > 8192)
      return {};
    Bytes b;
    if (!bytes(in.size() * 138 / 100 + 1, b, [&](auto *r) {
          return dkcrypt_encode(DKCRYPT_BASE58, view(in), r);
        }))
      return {};
    std::string out(b.begin(), b.end());
    SecureZero(b);
    return out;
  }
  static bool Base58Decode(const std::string &in, Bytes &out) {
    return bytes(in.size(), out, [&](auto *b) {
      return dkcrypt_decode(
          DKCRYPT_BASE58,
          {reinterpret_cast<const uint8_t *>(in.data()), in.size()}, b);
    });
  }

private:
  static bool EdPair(dkcrypt_span seed, Ed25519KeyPair &out) {
    std::array<uint8_t, 96> raw{};
    bool ok =
        array(raw, [&](auto *b) { return dkcrypt_ed25519_keypair(seed, b); });
    std::copy_n(raw.begin(), 32, out.public_key.begin());
    std::copy_n(raw.begin() + 32, 64, out.secret_key.begin());
    SecureZero(raw);
    return ok;
  }
  static bool XPair(dkcrypt_span secret, X25519KeyPair &out) {
    std::array<uint8_t, 64> raw{};
    bool ok =
        array(raw, [&](auto *b) { return dkcrypt_x25519_keypair(secret, b); });
    std::copy_n(raw.begin(), 32, out.public_key.begin());
    std::copy_n(raw.begin() + 32, 32, out.secret_key.begin());
    SecureZero(raw);
    return ok;
  }

public:
  static bool GenerateEd25519KeyPair(Ed25519KeyPair &out) {
    return EdPair({}, out);
  }
  static bool Ed25519KeyPairFromSeed(const Ed25519Seed &seed,
                                     Ed25519KeyPair &out) {
    return EdPair(view(seed), out);
  }
  static bool Ed25519PublicFromSeed(const Ed25519Seed &seed,
                                    Ed25519PublicKey &out) {
    Ed25519KeyPair pair;
    bool ok = EdPair(view(seed), pair);
    out = pair.public_key;
    SecureZero(pair.secret_key);
    return ok;
  }
  static bool Ed25519Sign(std::span<const uint8_t> in,
                          const Ed25519SecretKey &key, Ed25519Signature &out) {
    return array(out, [&](auto *b) {
      return dkcrypt_ed25519_sign(view(key), view(in), b);
    });
  }
  static bool Ed25519SignFromSeed(const Ed25519Seed &key,
                                  std::span<const uint8_t> in,
                                  Ed25519Signature &out) {
    return array(out, [&](auto *b) {
      return dkcrypt_ed25519_sign_seed(view(key), view(in), b);
    });
  }
  static bool Ed25519Verify(std::span<const uint8_t> in,
                            const Ed25519Signature &sig,
                            const Ed25519PublicKey &key) {
    return dkcrypt_ed25519_verify(view(key), view(in), view(sig)) == DKCRYPT_OK;
  }
  static bool GenerateX25519KeyPair(X25519KeyPair &out) {
    return XPair({}, out);
  }
  static bool X25519KeyPairFromSecret(const X25519SecretKey &secret,
                                      X25519KeyPair &out) {
    return XPair(view(secret), out);
  }
  static bool X25519DeriveSharedSecret(const X25519SecretKey &secret,
                                       const X25519PublicKey &key,
                                       X25519SharedSecret &out) {
    return array(out, [&](auto *b) {
      return dkcrypt_x25519_shared(view(secret), view(key), b);
    });
  }
  static bool Seal(std::span<const uint8_t> in, const X25519PublicKey &key,
                   Bytes &out) {
    if (in.size() > DKCRYPT_MAX_INPUT) {
      out.clear();
      return false;
    }
    return bytes(in.size() + 48, out,
                 [&](auto *b) { return dkcrypt_seal(view(key), view(in), b); });
  }
  static bool OpenSealed(std::span<const uint8_t> in, const X25519KeyPair &key,
                         Bytes &out) {
    return bytes(in.size() >= 48 ? in.size() - 48 : 0, out, [&](auto *b) {
      return dkcrypt_open_sealed(view(key.public_key), view(key.secret_key),
                                 view(in), b);
    });
  }
  static bool XChaCha20Poly1305Encrypt(std::span<const uint8_t> in,
                                       std::span<const uint8_t> aad,
                                       const XChaCha20Poly1305Key &key,
                                       const XChaCha20Poly1305Nonce &nonce,
                                       Bytes &out) {
    if (in.size() > DKCRYPT_MAX_INPUT) {
      out.clear();
      return false;
    }
    return bytes(in.size() + 16, out, [&](auto *b) {
      return dkcrypt_aead_encrypt(view(key), view(nonce), view(aad), view(in),
                                  b);
    });
  }
  static bool XChaCha20Poly1305Decrypt(std::span<const uint8_t> in,
                                       std::span<const uint8_t> aad,
                                       const XChaCha20Poly1305Key &key,
                                       const XChaCha20Poly1305Nonce &nonce,
                                       Bytes &out) {
    return bytes(in.size() >= 16 ? in.size() - 16 : 0, out, [&](auto *b) {
      return dkcrypt_aead_decrypt(view(key), view(nonce), view(aad), view(in),
                                  b);
    });
  }
  class Sha256Stream {
    dkcrypt_sha256 *handle_ = nullptr;

  public:
    Sha256Stream() {
      if (dkcrypt_sha256_create(&handle_) != DKCRYPT_OK)
        throw std::runtime_error("libcrypt SHA256 initialization failed");
    }
    ~Sha256Stream() { dkcrypt_sha256_destroy(handle_); }
    Sha256Stream(const Sha256Stream &) = delete;
    Sha256Stream &operator=(const Sha256Stream &) = delete;
    Sha256Stream(Sha256Stream &&other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    Sha256Stream &operator=(Sha256Stream &&other) noexcept {
      if (this != &other) {
        dkcrypt_sha256_destroy(handle_);
        handle_ = std::exchange(other.handle_, nullptr);
      }
      return *this;
    }
    bool Update(std::span<const uint8_t> in) {
      return dkcrypt_sha256_update(handle_, view(in)) == DKCRYPT_OK;
    }
    bool Finalize(Hash256 &out) {
      return array(out,
                   [&](auto *b) { return dkcrypt_sha256_final(handle_, b); });
    }
    void Reset() noexcept {
      if (handle_)
        dkcrypt_sha256_reset(handle_);
    }
  };
  class NoiseXX {
    dkcrypt_noise *handle_ = nullptr;

  public:
    enum class Role : uint8_t { initiator, responder };
    NoiseXX(Role role, const X25519KeyPair &key,
            std::span<const uint8_t> prologue = {}) {
      if (dkcrypt_noise_create(
              role == Role::initiator ? DKCRYPT_INITIATOR : DKCRYPT_RESPONDER,
              view(key.secret_key), view(prologue), &handle_) != DKCRYPT_OK)
        throw std::runtime_error("libcrypt Noise initialization failed");
    }
    ~NoiseXX() { dkcrypt_noise_destroy(handle_); }
    NoiseXX(const NoiseXX &) = delete;
    NoiseXX &operator=(const NoiseXX &) = delete;
    NoiseXX(NoiseXX &&other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    NoiseXX &operator=(NoiseXX &&other) noexcept {
      if (this != &other) {
        dkcrypt_noise_destroy(handle_);
        handle_ = std::exchange(other.handle_, nullptr);
      }
      return *this;
    }
    bool WriteMessage(std::span<const uint8_t> in, Bytes &out) {
      if (in.size() > 65535) {
        out.clear();
        return false;
      }
      return bytes(in.size() + 96, out, [&](auto *b) {
        return dkcrypt_noise_write(handle_, view(in), b);
      });
    }
    bool ReadMessage(std::span<const uint8_t> in, Bytes &out) {
      return bytes(in.size(), out, [&](auto *b) {
        return dkcrypt_noise_read(handle_, view(in), b);
      });
    }
    bool complete() const noexcept {
      int32_t v = 0;
      return dkcrypt_noise_complete(handle_, &v) == DKCRYPT_OK && v;
    }
    bool GetHandshakeHash(Hash256 &out) const {
      return array(out,
                   [&](auto *b) { return dkcrypt_noise_hash(handle_, b); });
    }
    bool EncryptTransport(std::span<const uint8_t> in, Bytes &out) {
      if (in.size() > 65519) {
        out.clear();
        return false;
      }
      return bytes(in.size() + 16, out, [&](auto *b) {
        return dkcrypt_noise_encrypt(handle_, view(in), b);
      });
    }
    bool DecryptTransport(std::span<const uint8_t> in, Bytes &out) {
      return bytes(in.size() >= 16 ? in.size() - 16 : 0, out, [&](auto *b) {
        return dkcrypt_noise_decrypt(handle_, view(in), b);
      });
    }
  };
};
} // namespace devkit
