#include "stdafx.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/hmac.h>
#include <openssl/md2.h>
#include <openssl/md5.h>
#include <openssl/rc5.h>
#include <openssl/rand.h>
#include <openssl/aes.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/buffer.h>
#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <openssl/param_build.h>
#include <openssl/encoder.h>
#include <openssl/decoder.h>
int sk_ossl_md5(const char *src, size_t srcLen, char **dst, size_t *dstLen) {
  int r = SK_NO;
  *dst = NULL;
  *dstLen = 0;
  EVP_MD_CTX *ctx = NULL;
  do {
    if ((ctx = EVP_MD_CTX_new()) == NULL)
      break;
    if (1 != EVP_DigestInit_ex(ctx, EVP_md5(), NULL))
      break;
    if (1 != EVP_DigestUpdate(ctx, src ? src : NULL, srcLen))
      break;
    *dst = sk__malloc(4096);
    if (1 != EVP_DigestFinal_ex(ctx, (unsigned char *)(*dst),
                                (unsigned int *)dstLen))
      break;
    (*dst)[*dstLen] = 0;
    r = SK_OK;
  } while (0);
  if (ctx)
    EVP_MD_CTX_free(ctx);
  if (r != SK_OK) {
    sk__free(*dst);
    *dstLen = 0;
  }
  return r;
}