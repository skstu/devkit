#include "stdafx.h"
#include "zstd.h"
#include "zlib.h"
#define GZIP_CHUNK 16384
#define GZIP_WINDOW_BITS 15
#define GZIP_ENCODING 16

int sk_comp_zip(const char *src, size_t srcLen, char **dst, size_t *dstLen) {
  int r = SK_NO;
  *dst = NULL;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    uLong compSize = compressBound(srcLen);
    if (compSize <= 0)
      break;
    *dst = sk__malloc(compSize);
    if (!*dst)
      break;
    uLongf zipSize = (uLongf)compSize;
    if (Z_OK !=
        compress((Bytef *)(*dst), &zipSize, (Bytef *)src, (uLong)srcLen))
      break;
    *dstLen = zipSize;
    r = SK_OK;
  } while (0);
  if (r != SK_OK) {
    sk__free(*dst);
    *dst = NULL;
    *dstLen = 0;
  }
  return SK_NO;
}
int sk_comp_unzip(const char *src, size_t srcLen, char **dst, size_t *dstLen) {
  int r = SK_NO;
  *dst = NULL;
  do {
    if (!src || srcLen <= 0 || *dstLen <= 0)
      break;
    *dst = sk__malloc(*dstLen);
    if (!*dst)
      break;
    if (Z_OK != uncompress((Bytef *)*dst, (uLongf *)dstLen, (const Bytef *)src,
                           (uLong)srcLen))
      break;
    r = SK_OK;
  } while (0);
  if (r != SK_OK) {
    sk__free(*dst);
    *dstLen = 0;
  }
  return SK_NO;
}

int sk_comp_gzip(const char *src, size_t srcLen, char **dst, size_t *dstLen) {
  int r = SK_NO;
  *dst = NULL;
  *dstLen = 0;
  do {
    unsigned char *chunk = (unsigned char *)sk__malloc(GZIP_CHUNK);
    if (!chunk)
      break;
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    if (Z_OK != deflateInit2_(&strm, -1, Z_DEFLATED,
                              GZIP_WINDOW_BITS | GZIP_ENCODING, 8, 0,
                              ZLIB_VERSION,
                              (int)sizeof(z_stream) /*Z_DEFAULT_STRATEGY*/))
      break;
    r = SK_OK;
    strm.next_in = (unsigned char *)src;
    strm.avail_in = (unsigned int)srcLen;
    do {
      strm.avail_out = GZIP_CHUNK;
      strm.next_out = chunk;
      if (Z_STREAM_ERROR == deflate(&strm, Z_FINISH)) {
        r = SK_NO;
        break;
      }
      size_t addSize = GZIP_CHUNK - strm.avail_out;
      if (addSize > 0) {
        *dst = sk__realloc(*dst, *dstLen + addSize);
        if (!*dst) {
          r = SK_NO;
          break;
        }
        sk__memcpy(*dst + (*dstLen), chunk, addSize);
        *dstLen += addSize;
      }
    } while (strm.avail_out == 0);
    sk__free(chunk);
    deflateEnd(&strm);
    if (r != SK_OK) {
      sk__free(*dst);
      *dstLen = 0;
      break;
    }
  } while (0);
  return r;
}
int sk_comp_ungzip(const char *src, size_t srcLen, char **dst, size_t *dstLen) {
  int r = SK_NO;
  *dst = NULL;
  *dstLen = 0;
  do {
    unsigned char *chunk = (unsigned char *)sk__malloc(GZIP_CHUNK);
    if (!chunk)
      break;
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    if (inflateInit2_(&strm, 16 + MAX_WBITS, ZLIB_VERSION,
                      (int)sizeof(z_stream)) != Z_OK)
      break;
    strm.avail_in = (unsigned int)srcLen;
    strm.next_in = (unsigned char *)src;
    r = SK_OK;
    do {
      strm.avail_out = GZIP_CHUNK;
      strm.next_out = chunk;
      int ret = inflate(&strm, Z_NO_FLUSH);
      if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
        r = SK_NO;
        break;
      }
      size_t addSize = GZIP_CHUNK - strm.avail_out;
      if (addSize > 0) {
        *dst = sk__realloc(*dst, *dstLen + addSize);
        if (!*dst) {
          r = SK_NO;
          break;
        }
        memcpy(*dst + (*dstLen), chunk, addSize);
        *dstLen += addSize;
      }
    } while (strm.avail_out == 0);
    sk__free(chunk);
    deflateEnd(&strm);
    if (r != SK_OK) {
      sk__free(*dst);
      *dstLen = 0;
      break;
    }
  } while (0);
  return r;
}
int sk_comp_zstd(const char *src, size_t srcLen, char **dst, size_t *dstLen) {
  int r = SK_NO;
  *dst = NULL;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    size_t compressedSize = ZSTD_compressBound(srcLen);
    if (compressedSize <= 0)
      break;
    *dst = sk__malloc(compressedSize);
    if (!*dst)
      break;
    *dstLen = ZSTD_compress(*dst, compressedSize, src, srcLen, 1);
    if (*dstLen <= 0)
      break;
    r = SK_OK;
  } while (0);
  return r;
}
int sk_comp_unzstd(const char *src, size_t srcLen, char **dst, size_t *dstLen) {
  int r = SK_NO;
  *dst = NULL;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    unsigned long long decomp_size = ZSTD_getFrameContentSize(src, srcLen);
    if (decomp_size <= 0)
      break;
    *dst = sk__malloc(decomp_size);
    if (!*dst)
      break;
    *dstLen = ZSTD_decompress(*dst, decomp_size, src, srcLen);
    if (*dstLen <= 0)
      break;
    r = SK_OK;
  } while (0);
  return r;
}