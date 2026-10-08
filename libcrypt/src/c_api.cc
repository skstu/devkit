#include <algorithm>
#include <cstring>
#include <libcrypt.h>
#include <libcrypt/crypt.h>
#include <limits>
#include <new>
#include <openssl/opensslv.h>
#include <sodium.h>
#include <utility>

namespace {
struct Failure {
  dkcrypt_status status;
};
void need(bool ok, dkcrypt_status status = DKCRYPT_INVALID_ARGUMENT) {
  if (!ok)
    throw Failure{status};
}
void input(dkcrypt_span s, size_t max = DKCRYPT_MAX_INPUT) {
  need(s.size == 0 || s.data);
  need(s.size <= max, DKCRYPT_LIMIT);
}
std::span<const uint8_t> span(dkcrypt_span s) { return {s.data, s.size}; }
template <class T> struct Secret {
  T value{};
  ~Secret() {
    Crypt::SecureZero(
        std::span(reinterpret_cast<uint8_t *>(&value), sizeof(value)));
  }
};
struct Bytes : Crypt::Bytes {
  ~Bytes() { Crypt::SecureZero(*this); }
};
template <class T> void fixed(dkcrypt_span s, T &value) {
  input(s);
  need(s.size == value.size());
  std::copy_n(s.data, s.size, value.data());
}
void room(dkcrypt_buffer *o, size_t n) {
  need(o);
  need(o->capacity == 0 || o->data);
  if (o->capacity < n) {
    o->size = n;
    throw Failure{DKCRYPT_BUFFER_TOO_SMALL};
  }
}
void copy(dkcrypt_buffer *o, std::span<const uint8_t> value) {
  if (!value.empty())
    std::memcpy(o->data, value.data(), value.size());
  o->size = value.size();
}
template <class F> dkcrypt_status guard(F &&fn) noexcept {
  try {
    fn();
    return DKCRYPT_OK;
  } catch (const Failure &e) {
    return e.status;
  } catch (const std::bad_alloc &) {
    return DKCRYPT_NO_MEMORY;
  } catch (...) {
    return DKCRYPT_INTERNAL_ERROR;
  }
}
template <class F> dkcrypt_status output(dkcrypt_buffer *o, F &&fn) noexcept {
  if (!o)
    return DKCRYPT_INVALID_ARGUMENT;
  o->size = 0;
  auto status = guard([&] {
    need(!o->capacity || o->data);
    fn();
  });
  if (status != DKCRYPT_OK && status != DKCRYPT_BUFFER_TOO_SMALL)
    o->size = 0;
  return status;
}
std::string text(dkcrypt_span s) {
  return s.size ? std::string(reinterpret_cast<const char *>(s.data), s.size)
                : std::string{};
}
} // namespace
struct dkcrypt_sha256 {
  Crypt::Sha256Stream hash;
};
struct dkcrypt_noise {
  Crypt::NoiseXX noise;
  uint32_t role, step = 0;
  bool failed = false;
  dkcrypt_noise(uint32_t r, const Crypt::X25519KeyPair &key,
                dkcrypt_span prologue)
      : noise(r == DKCRYPT_INITIATOR ? Crypt::NoiseXX::Role::initiator
                                     : Crypt::NoiseXX::Role::responder,
              key, span(prologue)),
        role(r) {}
};
extern "C" {
uint32_t DKCRYPT_CALL dkcrypt_abi_version() { return DKCRYPT_ABI_VERSION; }
dkcrypt_status DKCRYPT_CALL dkcrypt_initialize(uint32_t abi) {
  if (abi != DKCRYPT_ABI_VERSION)
    return DKCRYPT_ABI_MISMATCH;
  return Crypt::Initialize() ? DKCRYPT_OK : DKCRYPT_INTERNAL_ERROR;
}
const char *DKCRYPT_CALL dkcrypt_backend_versions() {
  return "libsodium " SODIUM_VERSION_STRING "; " OPENSSL_VERSION_TEXT;
}
void DKCRYPT_CALL dkcrypt_secure_zero(void *data, size_t size) {
  if (data && size)
    sodium_memzero(data, size);
}
dkcrypt_status DKCRYPT_CALL dkcrypt_random(dkcrypt_buffer *o, size_t count) {
  return output(o, [&] {
    need(count <= DKCRYPT_MAX_INPUT, DKCRYPT_LIMIT);
    room(o, count);
    need(Crypt::RandomFill({o->data, count}), DKCRYPT_INTERNAL_ERROR);
    o->size = count;
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_equal(dkcrypt_span a, dkcrypt_span b,
                                          int32_t *equal) {
  if (equal)
    *equal = 0;
  return guard([&] {
    need(equal);
    input(a);
    input(b);
    *equal = Crypt::ConstantTimeEqual(span(a), span(b));
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_hash(uint32_t algorithm, dkcrypt_span in,
                                         dkcrypt_buffer *o) {
  return output(o, [&] {
    input(in);
    need(algorithm >= DKCRYPT_SHA256 && algorithm <= DKCRYPT_MD5_LEGACY);
    const size_t size = algorithm == DKCRYPT_SHA512       ? 64
                        : algorithm == DKCRYPT_MD5_LEGACY ? 16
                                                          : 32;
    room(o, size);
    if (algorithm == DKCRYPT_SHA512) {
      Crypt::Hash512 h{};
      need(Crypt::Sha512(span(in), h), DKCRYPT_INTERNAL_ERROR);
      copy(o, h);
    } else if (algorithm == DKCRYPT_MD5_LEGACY) {
      Crypt::Hash128 h{};
      need(Crypt::Md5(span(in), h), DKCRYPT_INTERNAL_ERROR);
      copy(o, h);
    } else {
      Crypt::Hash256 h{};
      need(algorithm == DKCRYPT_SHA256 ? Crypt::Sha256(span(in), h)
                                       : Crypt::Blake2b256(span(in), h),
           DKCRYPT_INTERNAL_ERROR);
      copy(o, h);
    }
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_hmac_sha256(dkcrypt_span key,
                                                dkcrypt_span in,
                                                dkcrypt_buffer *o) {
  return output(o, [&] {
    input(in);
    Secret<Crypt::HmacSha256Key> k;
    fixed(key, k.value);
    room(o, 32);
    Secret<Crypt::Hash256> h;
    need(Crypt::HmacSha256(span(in), k.value, h.value), DKCRYPT_INTERNAL_ERROR);
    copy(o, h.value);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_kdf_blake2b(dkcrypt_span key,
                                                dkcrypt_span context,
                                                uint64_t id,
                                                dkcrypt_buffer *o) {
  return output(o, [&] {
    Secret<Crypt::Hash256> k, h;
    fixed(key, k.value);
    input(context);
    need(context.size == 8);
    room(o, 32);
    need(Crypt::Initialize(), DKCRYPT_INTERNAL_ERROR);
    need(crypto_kdf_derive_from_key(
             h.value.data(), 32, id,
             reinterpret_cast<const char *>(context.data), k.value.data()) == 0,
         DKCRYPT_INTERNAL_ERROR);
    copy(o, h.value);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_argon2id(dkcrypt_span password,
                                             dkcrypt_span salt,
                                             uint64_t operations,
                                             uint64_t memory,
                                             dkcrypt_buffer *o) {
  return output(o, [&] {
    input(password, 1024);
    need(password.size > 0);
    Secret<Crypt::PasswordHashSalt> s;
    fixed(salt, s.value);
    need(operations >= 1 && operations <= 10 && memory >= 8192 &&
             memory <= 256ull * 1024 * 1024,
         DKCRYPT_LIMIT);
    room(o, 32);
    Secret<Crypt::Hash256> h;
    need(Crypt::Argon2id(
             {reinterpret_cast<const char *>(password.data), password.size},
             s.value, operations, size_t(memory), h.value),
         DKCRYPT_INTERNAL_ERROR);
    copy(o, h.value);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_encode(uint32_t encoding, dkcrypt_span in,
                                           dkcrypt_buffer *o) {
  return output(o, [&] {
    input(in, encoding == DKCRYPT_BASE58 ? 8192 : DKCRYPT_MAX_INPUT);
    need(encoding == DKCRYPT_BASE64 || encoding == DKCRYPT_BASE58);
    const size_t bound = encoding == DKCRYPT_BASE64 ? ((in.size + 2) / 3) * 4
                                                    : in.size * 138 / 100 + 1;
    room(o, bound);
    std::string result;
    if (encoding == DKCRYPT_BASE64)
      need(Crypt::Base64Encode(span(in), result), DKCRYPT_INTERNAL_ERROR);
    else
      result = Crypt::Base58Encode(span(in));
    copy(o, {reinterpret_cast<const uint8_t *>(result.data()), result.size()});
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_decode(uint32_t encoding, dkcrypt_span in,
                                           dkcrypt_buffer *o) {
  return output(o, [&] {
    input(in, encoding == DKCRYPT_BASE58 ? 11305 : DKCRYPT_MAX_INPUT);
    need(encoding == DKCRYPT_BASE64 || encoding == DKCRYPT_BASE58);
    room(o, in.size);
    Bytes result;
    auto encoded = text(in);
    need(encoding == DKCRYPT_BASE64 ? Crypt::Base64Decode(encoded, result)
                                    : Crypt::Base58Decode(encoded, result));
    copy(o, result);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_ed25519_keypair(dkcrypt_span seed,
                                                    dkcrypt_buffer *o) {
  return output(o, [&] {
    input(seed);
    need(seed.size == 0 || seed.size == 32);
    room(o, 96);
    Secret<Crypt::Ed25519KeyPair> key;
    if (seed.size) {
      Secret<Crypt::Ed25519Seed> s;
      fixed(seed, s.value);
      need(Crypt::Ed25519KeyPairFromSeed(s.value, key.value),
           DKCRYPT_INTERNAL_ERROR);
    } else
      need(Crypt::GenerateEd25519KeyPair(key.value), DKCRYPT_INTERNAL_ERROR);
    std::memcpy(o->data, key.value.public_key.data(), 32);
    std::memcpy(o->data + 32, key.value.secret_key.data(), 64);
    o->size = 96;
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_ed25519_sign(dkcrypt_span secret,
                                                 dkcrypt_span in,
                                                 dkcrypt_buffer *o) {
  return output(o, [&] {
    input(in);
    Secret<Crypt::Ed25519SecretKey> k;
    fixed(secret, k.value);
    room(o, 64);
    Crypt::Ed25519Signature sig{};
    need(Crypt::Ed25519Sign(span(in), k.value, sig), DKCRYPT_INTERNAL_ERROR);
    copy(o, sig);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_ed25519_sign_seed(dkcrypt_span seed,
                                                      dkcrypt_span in,
                                                      dkcrypt_buffer *o) {
  return output(o, [&] {
    input(in);
    Secret<Crypt::Ed25519Seed> k;
    fixed(seed, k.value);
    room(o, 64);
    Crypt::Ed25519Signature sig{};
    need(Crypt::Ed25519SignFromSeed(k.value, span(in), sig),
         DKCRYPT_INTERNAL_ERROR);
    copy(o, sig);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_ed25519_verify(dkcrypt_span key,
                                                   dkcrypt_span in,
                                                   dkcrypt_span signature) {
  return guard([&] {
    input(in);
    Crypt::Ed25519PublicKey k{};
    Crypt::Ed25519Signature sig{};
    fixed(key, k);
    fixed(signature, sig);
    need(Crypt::Ed25519Verify(span(in), sig, k), DKCRYPT_AUTH_FAILED);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_x25519_keypair(dkcrypt_span secret,
                                                   dkcrypt_buffer *o) {
  return output(o, [&] {
    input(secret);
    need(secret.size == 0 || secret.size == 32);
    room(o, 64);
    Secret<Crypt::X25519KeyPair> k;
    if (secret.size) {
      Secret<Crypt::X25519SecretKey> s;
      fixed(secret, s.value);
      need(Crypt::X25519KeyPairFromSecret(s.value, k.value),
           DKCRYPT_INTERNAL_ERROR);
    } else
      need(Crypt::GenerateX25519KeyPair(k.value), DKCRYPT_INTERNAL_ERROR);
    std::memcpy(o->data, k.value.public_key.data(), 32);
    std::memcpy(o->data + 32, k.value.secret_key.data(), 32);
    o->size = 64;
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_x25519_shared(dkcrypt_span secret,
                                                  dkcrypt_span key,
                                                  dkcrypt_buffer *o) {
  return output(o, [&] {
    Secret<Crypt::X25519SecretKey> s;
    Crypt::X25519PublicKey k{};
    fixed(secret, s.value);
    fixed(key, k);
    room(o, 32);
    Secret<Crypt::X25519SharedSecret> h;
    need(Crypt::X25519DeriveSharedSecret(s.value, k, h.value),
         DKCRYPT_AUTH_FAILED);
    copy(o, h.value);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_seal(dkcrypt_span key, dkcrypt_span in,
                                         dkcrypt_buffer *o) {
  return output(o, [&] {
    input(in);
    Crypt::X25519PublicKey k{};
    fixed(key, k);
    room(o, in.size + 48);
    Bytes result;
    need(Crypt::Seal(span(in), k, result), DKCRYPT_INTERNAL_ERROR);
    copy(o, result);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_open_sealed(dkcrypt_span key,
                                                dkcrypt_span secret,
                                                dkcrypt_span in,
                                                dkcrypt_buffer *o) {
  return output(o, [&] {
    input(in, DKCRYPT_MAX_INPUT + 48);
    need(in.size >= 48);
    Secret<Crypt::X25519KeyPair> k;
    fixed(key, k.value.public_key);
    fixed(secret, k.value.secret_key);
    room(o, in.size - 48);
    Bytes result;
    need(Crypt::OpenSealed(span(in), k.value, result), DKCRYPT_AUTH_FAILED);
    copy(o, result);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_aead_encrypt(dkcrypt_span key,
                                                 dkcrypt_span nonce,
                                                 dkcrypt_span aad,
                                                 dkcrypt_span in,
                                                 dkcrypt_buffer *o) {
  return output(o, [&] {
    input(in);
    input(aad);
    Secret<Crypt::XChaCha20Poly1305Key> k;
    Crypt::XChaCha20Poly1305Nonce n{};
    fixed(key, k.value);
    fixed(nonce, n);
    room(o, in.size + 16);
    Bytes result;
    need(Crypt::XChaCha20Poly1305Encrypt(span(in), span(aad), k.value, n,
                                         result),
         DKCRYPT_INTERNAL_ERROR);
    copy(o, result);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_aead_decrypt(dkcrypt_span key,
                                                 dkcrypt_span nonce,
                                                 dkcrypt_span aad,
                                                 dkcrypt_span in,
                                                 dkcrypt_buffer *o) {
  return output(o, [&] {
    input(in, DKCRYPT_MAX_INPUT + 16);
    input(aad);
    need(in.size >= 16);
    Secret<Crypt::XChaCha20Poly1305Key> k;
    Crypt::XChaCha20Poly1305Nonce n{};
    fixed(key, k.value);
    fixed(nonce, n);
    room(o, in.size - 16);
    Bytes result;
    need(Crypt::XChaCha20Poly1305Decrypt(span(in), span(aad), k.value, n,
                                         result),
         DKCRYPT_AUTH_FAILED);
    copy(o, result);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_sha256_create(dkcrypt_sha256 **out) {
  if (out)
    *out = nullptr;
  return guard([&] {
    need(out);
    need(Crypt::Initialize(), DKCRYPT_INTERNAL_ERROR);
    *out = new dkcrypt_sha256;
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_sha256_update(dkcrypt_sha256 *h,
                                                  dkcrypt_span in) {
  return guard([&] {
    need(h);
    input(in);
    need(h->hash.Update(span(in)), DKCRYPT_INVALID_STATE);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_sha256_final(dkcrypt_sha256 *h,
                                                 dkcrypt_buffer *o) {
  return output(o, [&] {
    need(h);
    room(o, 32);
    Crypt::Hash256 result{};
    need(h->hash.Finalize(result), DKCRYPT_INVALID_STATE);
    copy(o, result);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_sha256_reset(dkcrypt_sha256 *h) {
  return guard([&] {
    need(h);
    h->hash.Reset();
  });
}
void DKCRYPT_CALL dkcrypt_sha256_destroy(dkcrypt_sha256 *h) { delete h; }
dkcrypt_status DKCRYPT_CALL dkcrypt_noise_create(uint32_t role,
                                                 dkcrypt_span secret,
                                                 dkcrypt_span prologue,
                                                 dkcrypt_noise **out) {
  if (out)
    *out = nullptr;
  return guard([&] {
    need(out);
    need(role == DKCRYPT_INITIATOR || role == DKCRYPT_RESPONDER);
    input(prologue, 65535);
    Secret<Crypt::X25519SecretKey> s;
    fixed(secret, s.value);
    Secret<Crypt::X25519KeyPair> key;
    need(Crypt::X25519KeyPairFromSecret(s.value, key.value),
         DKCRYPT_INTERNAL_ERROR);
    *out = new dkcrypt_noise(role, key.value, prologue);
  });
}
}
namespace {
template <class F>
dkcrypt_status noise_output(dkcrypt_noise *s, dkcrypt_buffer *out, F &&fn) {
  auto status = output(out, [&] {
    need(s);
    need(!s->failed, DKCRYPT_INVALID_STATE);
    fn();
  });
  if (s && (status == DKCRYPT_NO_MEMORY || status == DKCRYPT_INTERNAL_ERROR))
    s->failed = true;
  return status;
}
bool writing(const dkcrypt_noise *s) {
  return s->role == DKCRYPT_INITIATOR ? s->step != 1 : s->step == 1;
}
size_t overhead(const dkcrypt_noise *s) {
  return s->step == 0 ? 32 : s->step == 1 ? 96 : 64;
}
} // namespace
extern "C" {
dkcrypt_status DKCRYPT_CALL dkcrypt_noise_write(dkcrypt_noise *s,
                                                dkcrypt_span in,
                                                dkcrypt_buffer *o) {
  return noise_output(s, o, [&] {
    need(s->step < 3 && writing(s), DKCRYPT_INVALID_STATE);
    input(in, 65535 - overhead(s));
    room(o, in.size + overhead(s));
    Bytes result;
    if (!s->noise.WriteMessage(span(in), result)) {
      s->failed = true;
      throw Failure{DKCRYPT_INVALID_STATE};
    }
    ++s->step;
    copy(o, result);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_noise_read(dkcrypt_noise *s,
                                               dkcrypt_span in,
                                               dkcrypt_buffer *o) {
  return noise_output(s, o, [&] {
    need(s->step < 3 && !writing(s), DKCRYPT_INVALID_STATE);
    input(in, 65535);
    need(in.size >= overhead(s));
    room(o, in.size - overhead(s));
    Bytes result;
    if (!s->noise.ReadMessage(span(in), result)) {
      s->failed = true;
      throw Failure{DKCRYPT_AUTH_FAILED};
    }
    ++s->step;
    copy(o, result);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_noise_complete(dkcrypt_noise *s,
                                                   int32_t *complete) {
  if (complete)
    *complete = 0;
  return guard([&] {
    need(s && complete);
    *complete = !s->failed && s->noise.complete();
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_noise_hash(dkcrypt_noise *s,
                                               dkcrypt_buffer *o) {
  return noise_output(s, o, [&] {
    room(o, 32);
    Crypt::Hash256 result{};
    need(s->noise.GetHandshakeHash(result), DKCRYPT_INVALID_STATE);
    copy(o, result);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_noise_encrypt(dkcrypt_noise *s,
                                                  dkcrypt_span in,
                                                  dkcrypt_buffer *o) {
  return noise_output(s, o, [&] {
    need(s->noise.complete(), DKCRYPT_INVALID_STATE);
    input(in, 65519);
    room(o, in.size + 16);
    Bytes result;
    need(s->noise.EncryptTransport(span(in), result), DKCRYPT_INVALID_STATE);
    copy(o, result);
  });
}
dkcrypt_status DKCRYPT_CALL dkcrypt_noise_decrypt(dkcrypt_noise *s,
                                                  dkcrypt_span in,
                                                  dkcrypt_buffer *o) {
  return noise_output(s, o, [&] {
    need(s->noise.complete(), DKCRYPT_INVALID_STATE);
    input(in, 65535);
    need(in.size >= 16);
    room(o, in.size - 16);
    Bytes result;
    need(s->noise.DecryptTransport(span(in), result), DKCRYPT_AUTH_FAILED);
    copy(o, result);
  });
}
void DKCRYPT_CALL dkcrypt_noise_destroy(dkcrypt_noise *s) { delete s; }
}
