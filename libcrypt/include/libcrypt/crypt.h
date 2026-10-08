#ifndef DEVKIT_CRYPT_H
#define DEVKIT_CRYPT_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
#define DKCRYPT_CALL __cdecl
#if defined(DEVKIT_CRYPT_BUILD)
#define DKCRYPT_API __declspec(dllexport)
#else
#define DKCRYPT_API __declspec(dllimport)
#endif
#else
#define DKCRYPT_CALL
#define DKCRYPT_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define DKCRYPT_ABI_VERSION 1u
#define DKCRYPT_MAX_INPUT (64u * 1024u * 1024u)
typedef int32_t dkcrypt_status;
enum {
  DKCRYPT_OK = 0,
  DKCRYPT_INVALID_ARGUMENT = 1,
  DKCRYPT_BUFFER_TOO_SMALL = 2,
  DKCRYPT_AUTH_FAILED = 3,
  DKCRYPT_INVALID_STATE = 4,
  DKCRYPT_LIMIT = 5,
  DKCRYPT_NO_MEMORY = 6,
  DKCRYPT_INTERNAL_ERROR = 7,
  DKCRYPT_ABI_MISMATCH = 8
};
enum {
  DKCRYPT_SHA256 = 1,
  DKCRYPT_SHA512 = 2,
  DKCRYPT_BLAKE2B256 = 3,
  DKCRYPT_MD5_LEGACY = 4
};
enum { DKCRYPT_BASE64 = 1, DKCRYPT_BASE58 = 2 };
enum { DKCRYPT_INITIATOR = 1, DKCRYPT_RESPONDER = 2 };
typedef struct dkcrypt_span {
  const uint8_t *data;
  size_t size;
} dkcrypt_span;
typedef struct dkcrypt_buffer {
  uint8_t *data;
  size_t capacity;
  size_t size;
} dkcrypt_buffer;
typedef struct dkcrypt_sha256 dkcrypt_sha256;
typedef struct dkcrypt_noise dkcrypt_noise;
/* Synchronous C ABI. No exception crosses it. Callers own input/output storage.
 * Nonempty spans need valid memory. Output structures cannot alias inputs.
 * BUFFER_TOO_SMALL reports a sufficient capacity in size; no state is consumed.
 * Other failures report size=0. Inspect status before reading output.
 * Stateless functions are thread-safe. Each handle must be externally
 * serialized, including destroy. Destroy(NULL) is safe; using a
 * destroyed/foreign handle is not. Keys are raw bytes, not passwords or encoded
 * strings; caller wipes its own copies. Version strings have process lifetime;
 * do not free them. */
DKCRYPT_API uint32_t DKCRYPT_CALL dkcrypt_abi_version(void);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_initialize(uint32_t abi);
DKCRYPT_API const char *DKCRYPT_CALL dkcrypt_backend_versions(void);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_random(dkcrypt_buffer *output,
                                                       size_t count);
DKCRYPT_API void DKCRYPT_CALL dkcrypt_secure_zero(void *data, size_t size);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_equal(dkcrypt_span a,
                                                      dkcrypt_span b,
                                                      int32_t *equal);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_hash(uint32_t algorithm,
                                                     dkcrypt_span input,
                                                     dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_hmac_sha256(
    dkcrypt_span key, dkcrypt_span input, dkcrypt_buffer *output);
/* libsodium BLAKE2b KDF: 32-byte key, exactly 8 context bytes, 32-byte output.
 * The caller owns domain/context/id assignment and its format version. */
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_kdf_blake2b(dkcrypt_span key, dkcrypt_span context, uint64_t id,
                    dkcrypt_buffer *output);
/* Argon2id v1.3, 16-byte salt, 32-byte result; password 1..1024 bytes,
 * operations 1..10, memory 8 KiB..256 MiB. Caller enforces aggregate
 * concurrency. */
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_argon2id(dkcrypt_span password, dkcrypt_span salt, uint64_t operations,
                 uint64_t memory, dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_encode(uint32_t encoding,
                                                       dkcrypt_span input,
                                                       dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_decode(uint32_t encoding,
                                                       dkcrypt_span input,
                                                       dkcrypt_buffer *output);
/* Empty seed/secret generates a random pair. Otherwise exactly 32 bytes.
 * Keypair output is public[32] || secret[64] (Ed25519), public[32] ||
 * secret[32] (X25519). */
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_ed25519_keypair(dkcrypt_span seed, dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_ed25519_sign(
    dkcrypt_span secret, dkcrypt_span input, dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_ed25519_sign_seed(
    dkcrypt_span seed, dkcrypt_span input, dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_ed25519_verify(
    dkcrypt_span public_key, dkcrypt_span input, dkcrypt_span signature);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_x25519_keypair(dkcrypt_span secret, dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_x25519_shared(
    dkcrypt_span secret, dkcrypt_span public_key, dkcrypt_buffer *output);
/* libsodium sealed boxes: X25519 + XSalsa20-Poly1305; overhead 48 bytes. */
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_seal(dkcrypt_span public_key,
                                                     dkcrypt_span input,
                                                     dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_open_sealed(dkcrypt_span public_key, dkcrypt_span secret,
                    dkcrypt_span input, dkcrypt_buffer *output);
/* XChaCha20-Poly1305-IETF: key32, nonce24, appended tag16.
 * A nonce MUST NOT repeat under the same key. AAD must match on decryption. */
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_aead_encrypt(dkcrypt_span key, dkcrypt_span nonce, dkcrypt_span aad,
                     dkcrypt_span input, dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_aead_decrypt(dkcrypt_span key, dkcrypt_span nonce, dkcrypt_span aad,
                     dkcrypt_span input, dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_sha256_create(dkcrypt_sha256 **output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_sha256_update(dkcrypt_sha256 *hash, dkcrypt_span input);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_sha256_final(dkcrypt_sha256 *hash, dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_sha256_reset(dkcrypt_sha256 *hash);
DKCRYPT_API void DKCRYPT_CALL dkcrypt_sha256_destroy(dkcrypt_sha256 *hash);
/* Noise revision34, Noise_XX_25519_ChaChaPoly_SHA256 only; fresh random
 * ephemeral keys; caller supplies static secret32 and prologue (<=65535 bytes).
 * Handshake/transport messages <=65535 bytes. No identity or trust is inferred.
 * Peer identity binding and authenticated application payload belong to the
 * caller. Write/read alternate according to XX. Authentication failure aborts
 * handshake; transport authentication failure does not advance receive nonce.
 * An internal exception poisons the handle; destroy it and create a new
 * session. */
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_noise_create(uint32_t role, dkcrypt_span secret, dkcrypt_span prologue,
                     dkcrypt_noise **output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_noise_write(
    dkcrypt_noise *session, dkcrypt_span input, dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_noise_read(
    dkcrypt_noise *session, dkcrypt_span input, dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_noise_complete(dkcrypt_noise *session, int32_t *complete);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL
dkcrypt_noise_hash(dkcrypt_noise *session, dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_noise_encrypt(
    dkcrypt_noise *session, dkcrypt_span input, dkcrypt_buffer *output);
DKCRYPT_API dkcrypt_status DKCRYPT_CALL dkcrypt_noise_decrypt(
    dkcrypt_noise *session, dkcrypt_span input, dkcrypt_buffer *output);
DKCRYPT_API void DKCRYPT_CALL dkcrypt_noise_destroy(dkcrypt_noise *session);
#ifdef __cplusplus
}
#endif
#endif
