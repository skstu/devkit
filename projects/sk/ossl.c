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
int sk_ossl_base64_encode(const char *src, size_t srcLen, char **dst,
                          size_t *dstLen) {
  int result = 0;
  *dst = NULL;
  *dstLen = 0;
  BIO *bmem = NULL;
  BIO *b64 = NULL;
  BUF_MEM *bptr = NULL;
  do {
    if (!src || srcLen <= 0)
      break;
    b64 = BIO_new(BIO_f_base64());
    if (!b64)
      break;
    if (!0)
      BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new(BIO_s_mem());
    if (!bmem)
      break;
    b64 = BIO_push(b64, bmem);
    if (!b64)
      break;
    long bio_write_length = BIO_write(b64, src, srcLen);
    if (bio_write_length <= 0)
      break;
    int sslret = BIO_flush(b64);
    if (sslret != 1)
      break;
    sslret = BIO_get_mem_ptr(b64, &bptr);
    if (sslret != 1)
      break;
    sslret = BIO_set_close(b64, BIO_NOCLOSE);
    if (sslret != 1)
      break;
    *dstLen = bptr->length;
    *dst = sk__malloc(bptr->length);
    sk__memcpy(*dst, bptr->data, bptr->length);
    result = 1;
  } while (0);
  if (b64) {
    BIO_free_all(b64);
    b64 = NULL;
  }
  if (bptr) {
    BUF_MEM_free(bptr);
    bptr = NULL;
  }
  return result;
}
int sk_ossl_base64_decode(const char *src, size_t srcLen, char **dst,
                          size_t *dstLen) {
  return 0;
}
#if 0
int sk_ossl_base64_encode(const std::string &input, std::string &output,
                          const bool &multiline /*= false*/) {
}
int sk_ossl_base64_decode(const std::string &input, std::string &output,
                          const bool &multiline /*= false*/) {
  int result = 0;
  output.clear();
  BIO *b64 = nullptr;
  BIO *bmem = nullptr;
  do {
    if (input.empty())
      break;
    b64 = BIO_new(BIO_f_base64());
    if (!b64)
      break;
    if (!multiline)
      BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new_mem_buf(input.data(), static_cast<int>(input.size()));
    if (!bmem)
      break;
    bmem = BIO_push(b64, bmem);
    if (!bmem)
      break;
    output.resize(input.size());
    long sslret = BIO_read(bmem, &output[0], static_cast<int>(output.size()));
    if (sslret <= 0) {
      output.clear();
      break;
    }
    output.resize(sslret);
    result = 1;
  } while (0);
  if (b64) {
    BIO_free_all(b64);
    b64 = nullptr;
  }
  return result;
}
#endif