#include <libcrypt/crypt.h>
#include <stdio.h>
int main(void) {
  uint8_t digest[32];
  dkcrypt_buffer out = {digest, sizeof digest, 0};
  dkcrypt_span input = {(const uint8_t *)"abc", 3};
  if (dkcrypt_initialize(DKCRYPT_ABI_VERSION) != DKCRYPT_OK ||
      dkcrypt_hash(DKCRYPT_SHA256, input, &out) != DKCRYPT_OK)
    return 1;
  for (size_t i = 0; i < out.size; ++i)
    printf("%02x", digest[i]);
  printf("\n%s\n", dkcrypt_backend_versions());
  return 0;
}
