#include "stdafx.h"

static const std::string gs_default_seed = R"(k34ubll5)";

std::string Ssl::MD5(const std::string &src, const bool &bin2hex) {
  std::string result;
  EVP_MD_CTX *ctx = NULL;
  do {
    if ((ctx = EVP_MD_CTX_new()) == NULL)
      break;
    if (1 != EVP_DigestInit_ex(ctx, EVP_md5(), NULL))
      break;
    if (1 != EVP_DigestUpdate(ctx, src.empty() ? NULL : src.data(), src.size()))
      break;
    unsigned int result_len = 0;
    result.resize(4096, 0x00);
    if (1 != EVP_DigestFinal_ex(ctx, (unsigned char *)&result[0],
                                (unsigned int *)&result_len))
      break;
    result.resize(result_len);
    if (bin2hex)
      result = stl::String::BinaryToHexString(result);
  } while (0);
  if (ctx)
    EVP_MD_CTX_free(ctx);
  return result;
}
std::string Ssl::Base64Encode(const std::string &src, const bool &multiline) {
  std::string result;
  BIO *bmem = NULL;
  BIO *b64 = NULL;
  BUF_MEM *bptr = NULL;
  do {
    if (src.empty())
      break;
    b64 = BIO_new(BIO_f_base64());
    if (!b64)
      break;
    if (multiline)
      BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new(BIO_s_mem());
    if (!bmem)
      break;
    b64 = BIO_push(b64, bmem);
    if (!b64)
      break;
    long bio_write_length = BIO_write(b64, src.data(), src.size());
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
    result.append(bptr->data, bptr->length);
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
std::string Ssl::Base64Decode(const std::string &src, const bool &multiline) {
  std::string result;
  BIO *b64 = nullptr;
  BIO *bmem = nullptr;
  do {
    if (src.empty())
      break;
    b64 = BIO_new(BIO_f_base64());
    if (!b64)
      break;
    if (!multiline)
      BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new_mem_buf(src.data(), static_cast<int>(src.size()));
    if (!bmem)
      break;
    bmem = BIO_push(b64, bmem);
    if (!bmem)
      break;
    result.resize(src.size());
    long sslret = BIO_read(bmem, &result[0], static_cast<int>(result.size()));
    if (sslret <= 0) {
      result.clear();
      break;
    }
    result.resize(sslret);
  } while (0);
  if (b64) {
    BIO_free_all(b64);
    b64 = nullptr;
  }
  return result;
}

std::string Ssl::SHA256(const std::string &src, const bool &bin2hex) {
  std::string result;
#if OSSL_ENABLE_OPENSSL_3
  bool success = false;
  SHA256_CTX sha256 = {0};
  int sslret = 0;
  do {
    if (src.empty())
      break;
    sslret = SHA256_Init(&sha256);
    if (1 != sslret)
      break;
    sslret = SHA256_Update(&sha256, (uint8_t *)&src[0], src.size());
    if (1 != sslret)
      break;
    result.resize(SHA256_DIGEST_LENGTH, 0x00);
    sslret = SHA256_Final((uint8_t *)&result[0], &sha256);
    if (1 != sslret)
      break;
    if (bin2hex)
      result = stl::String::BinaryToHexString(result);
    success = true;
  } while (0);
  if (!success)
    result.clear();
#endif // OSSL_ENABLE_OPENSSL_3
  return result;
}

std::string Ssl::HMAC_SHA1(const std::string &src, const std::string &seed,
                           const bool &bin2hex) {
  std::string result;
  HMAC_CTX *hmacCtx = nullptr;
  do {
    if (src.empty())
      break;
    hmacCtx = HMAC_CTX_new();
    if (!hmacCtx)
      break;
    std::string _seed = seed;
    if (_seed.empty())
      _seed = gs_default_seed;
    if (0 == HMAC_Init_ex(hmacCtx, (std::uint8_t *)&_seed[0],
                          static_cast<int>(_seed.size()), EVP_sha1(), nullptr))
      break;
    if (0 == HMAC_Update(hmacCtx, (const std::uint8_t *)src.data(), src.size()))
      break;
    std::uint8_t md[SHA_DIGEST_LENGTH] = {0};
    unsigned int md_size = 0;
    if (0 == HMAC_Final(hmacCtx, md, &md_size) || md_size <= 0)
      break;
    if (md_size <= 0)
      break;

    if (bin2hex)
      result =
          stl::String::BinaryToHexString(std::string((char *)&md[0], md_size));
    else
      result.append((char *)&md[0], md_size);
  } while (0);
  // HMAC_CTX_cleanup(hmacCtx);
  if (hmacCtx)
    HMAC_CTX_free(hmacCtx);
  return result;
}
std::string Ssl::HMAC_SHA256(const std::string &src, const std::string &seed,
                             const bool &bin2hex) {
  std::string result;
  HMAC_CTX *hmacCtx = nullptr;
  do {
    if (src.empty())
      break;
    hmacCtx = HMAC_CTX_new();
    if (!hmacCtx)
      break;
    std::string _seed = seed;
    if (_seed.empty())
      _seed = gs_default_seed;
    if (0 == HMAC_Init_ex(hmacCtx, (std::uint8_t *)&_seed[0],
                          static_cast<int>(_seed.size()), EVP_sha256(),
                          nullptr))
      break;
    if (0 == HMAC_Update(hmacCtx, (const std::uint8_t *)src.data(), src.size()))
      break;
    std::uint8_t md[SHA256_DIGEST_LENGTH] = {0};
    unsigned int md_size = 0;
    if (0 == HMAC_Final(hmacCtx, md, &md_size) || md_size <= 0)
      break;
    if (md_size <= 0)
      break;
    if (bin2hex)
      result =
          stl::String::BinaryToHexString(std::string((char *)&md[0], md_size));
    else
      result.append((char *)&md[0], md_size);
  } while (0);
  if (hmacCtx)
    HMAC_CTX_free(hmacCtx);
  return result;
}
