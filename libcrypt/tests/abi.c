#include <libcrypt/crypt.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "C ABI check failed at %d: %s\n", __LINE__, #x);         \
      return 1;                                                                \
    }                                                                          \
  } while (0)
static dkcrypt_span s(const void *p, size_t n) {
  dkcrypt_span v = {(const uint8_t *)p, n};
  return v;
}
int main(void) {
  static const uint8_t sha_abc[32] = {
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
      0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
      0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  uint8_t bytes[256], cipher[64], plain[64], key[32] = {0}, nonce[24] = {0},
                                             secret[32] = {1}, pair[64];
  dkcrypt_buffer out = {bytes, sizeof bytes, 0},
                 encrypted = {cipher, sizeof cipher, 0},
                 restored = {plain, sizeof plain, 0}, short_out = {bytes, 1, 0},
                 empty = {NULL, 0, 0}, pair_out = {pair, sizeof pair, 0};
  dkcrypt_sha256 *hash = NULL;
  dkcrypt_noise *i = NULL, *r = NULL;
  int32_t equal = 0, complete = 0;
  CHECK(dkcrypt_abi_version() == 1 &&
        dkcrypt_initialize(2) == DKCRYPT_ABI_MISMATCH &&
        dkcrypt_initialize(1) == DKCRYPT_OK);
  CHECK(dkcrypt_hash(DKCRYPT_SHA256, s("abc", 3), NULL) ==
        DKCRYPT_INVALID_ARGUMENT);
  CHECK(dkcrypt_hash(DKCRYPT_SHA256, s(NULL, 1), &out) ==
            DKCRYPT_INVALID_ARGUMENT &&
        out.size == 0);
  CHECK(dkcrypt_hash(99, s(NULL, 0), &out) == DKCRYPT_INVALID_ARGUMENT);
  CHECK(dkcrypt_hash(DKCRYPT_SHA256, s("abc", 3), &empty) ==
            DKCRYPT_BUFFER_TOO_SMALL &&
        empty.size == 32);
  CHECK(dkcrypt_hash(DKCRYPT_SHA256, s("abc", 3), &out) == DKCRYPT_OK &&
        out.size == 32 && !memcmp(bytes, sha_abc, 32));
  CHECK(dkcrypt_equal(s("same", 4), s("same", 4), &equal) == DKCRYPT_OK &&
        equal);
  CHECK(dkcrypt_equal(s("same", 4), s("sam", 3), &equal) == DKCRYPT_OK &&
        !equal);
  CHECK(dkcrypt_equal(s(NULL, 1), s(NULL, 0), &equal) ==
        DKCRYPT_INVALID_ARGUMENT);
  CHECK(dkcrypt_random(&out, DKCRYPT_MAX_INPUT + 1ull) == DKCRYPT_LIMIT &&
        out.size == 0);
  CHECK(dkcrypt_encode(DKCRYPT_BASE64, s("abc", 3), &out) == DKCRYPT_OK &&
        out.size == 4 && !memcmp(bytes, "YWJj", 4));
  CHECK(dkcrypt_decode(DKCRYPT_BASE64, s("!!!!", 4), &out) ==
            DKCRYPT_INVALID_ARGUMENT &&
        out.size == 0);
  CHECK(dkcrypt_sha256_create(&hash) == DKCRYPT_OK);
  CHECK(dkcrypt_sha256_update(hash, s("a", 1)) == DKCRYPT_OK);
  CHECK(dkcrypt_sha256_update(hash, s("bc", 2)) == DKCRYPT_OK);
  CHECK(dkcrypt_sha256_final(hash, &short_out) == DKCRYPT_BUFFER_TOO_SMALL);
  CHECK(dkcrypt_sha256_final(hash, &out) == DKCRYPT_OK &&
        !memcmp(bytes, sha_abc, 32));
  CHECK(dkcrypt_sha256_final(hash, &out) == DKCRYPT_INVALID_STATE);
  CHECK(dkcrypt_sha256_update(hash, s("abc", 3)) == DKCRYPT_INVALID_STATE);
  CHECK(dkcrypt_sha256_reset(hash) == DKCRYPT_OK);
  dkcrypt_sha256_destroy(hash);
  dkcrypt_sha256_destroy(NULL);
  CHECK(dkcrypt_aead_encrypt(s(key, 31), s(nonce, 24), s(NULL, 0),
                             s("secret", 6),
                             &encrypted) == DKCRYPT_INVALID_ARGUMENT);
  CHECK(dkcrypt_aead_encrypt(s(key, 32), s(nonce, 24), s("aad", 3),
                             s("secret", 6), &encrypted) == DKCRYPT_OK &&
        encrypted.size == 22);
  cipher[21] ^= 1;
  memset(plain, 0xa5, sizeof plain);
  CHECK(dkcrypt_aead_decrypt(s(key, 32), s(nonce, 24), s("aad", 3),
                             s(cipher, 22), &restored) == DKCRYPT_AUTH_FAILED &&
        restored.size == 0 && plain[0] == 0xa5);
  cipher[21] ^= 1;
  CHECK(dkcrypt_aead_decrypt(s(key, 32), s(nonce, 24), s("wrong", 5),
                             s(cipher, 22), &restored) == DKCRYPT_AUTH_FAILED);
  CHECK(dkcrypt_aead_decrypt(s(key, 32), s(nonce, 24), s("aad", 3),
                             s(cipher, 22), &restored) == DKCRYPT_OK &&
        restored.size == 6 && !memcmp(plain, "secret", 6));
  CHECK(dkcrypt_argon2id(s("pw", 2), s(key, 16), 3, UINT64_MAX, &out) ==
        DKCRYPT_LIMIT);
  CHECK(dkcrypt_x25519_keypair(s(secret, 32), &pair_out) == DKCRYPT_OK);
  CHECK(dkcrypt_x25519_shared(s(secret, 32), s(key, 32), &out) ==
        DKCRYPT_AUTH_FAILED);
  CHECK(dkcrypt_noise_create(DKCRYPT_INITIATOR, s(secret, 32), s(NULL, 0),
                             &i) == DKCRYPT_OK);
  secret[0] = 2;
  CHECK(dkcrypt_noise_create(DKCRYPT_RESPONDER, s(secret, 32), s(NULL, 0),
                             &r) == DKCRYPT_OK);
  CHECK(dkcrypt_noise_encrypt(i, s("x", 1), &out) == DKCRYPT_INVALID_STATE);
  CHECK(dkcrypt_noise_read(i, s(bytes, 32), &out) == DKCRYPT_INVALID_STATE);
  CHECK(dkcrypt_noise_write(i, s("one", 3), &short_out) ==
            DKCRYPT_BUFFER_TOO_SMALL &&
        short_out.size == 35);
  CHECK(dkcrypt_noise_write(i, s("one", 3), &out) == DKCRYPT_OK &&
        out.size == 35);
  CHECK(dkcrypt_noise_read(r, s(bytes, out.size), &empty) ==
            DKCRYPT_BUFFER_TOO_SMALL &&
        empty.size == 3);
  CHECK(dkcrypt_noise_read(r, s(bytes, out.size), &restored) == DKCRYPT_OK &&
        restored.size == 3);
  CHECK(dkcrypt_noise_write(r, s("two", 3), &out) == DKCRYPT_OK);
  CHECK(dkcrypt_noise_read(i, s(bytes, out.size), &restored) == DKCRYPT_OK);
  CHECK(dkcrypt_noise_write(i, s("three", 5), &out) == DKCRYPT_OK);
  CHECK(dkcrypt_noise_read(r, s(bytes, out.size), &restored) == DKCRYPT_OK);
  CHECK(dkcrypt_noise_complete(r, &complete) == DKCRYPT_OK && complete);
  CHECK(dkcrypt_noise_encrypt(i, s("message", 7), &short_out) ==
        DKCRYPT_BUFFER_TOO_SMALL);
  CHECK(dkcrypt_noise_encrypt(i, s("message", 7), &out) == DKCRYPT_OK &&
        out.size == 23);
  CHECK(dkcrypt_noise_decrypt(r, s(bytes, out.size), &empty) ==
            DKCRYPT_BUFFER_TOO_SMALL &&
        empty.size == 7);
  CHECK(dkcrypt_noise_decrypt(r, s(bytes, out.size), &restored) == DKCRYPT_OK &&
        restored.size == 7);
  CHECK(dkcrypt_noise_decrypt(r, s(bytes, out.size), &restored) ==
            DKCRYPT_AUTH_FAILED &&
        restored.size == 0);
  dkcrypt_noise_destroy(i);
  dkcrypt_noise_destroy(r);
  dkcrypt_noise_destroy(NULL);
  dkcrypt_secure_zero(key, sizeof key);
  dkcrypt_secure_zero(secret, sizeof secret);
  puts("PASS pure C ABI, vectors, sizing without state loss, tamper/replay and "
       "parameter bounds");
  return 0;
}
