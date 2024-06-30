#include "stdafx.h"

static const char ICU_CONV_TYPE_NAME[][100] = {
    "UNKNOWN",  "UTF-8",    "UTF-16", "UTF-16LE", "UTF-16BE", "UTF-32",
    "UTF-32LE", "UTF-32BE", "ASCII",  "GBK",      "GB18030",
};

Icu::Icu() {
  Init();
}
Icu::~Icu() {
  UnInit();
}
void Icu::Release() const {
  delete this;
}
bool Icu::Ready() const {
  return false;
}
void Icu::Init() {
}
void Icu::UnInit() {
}
bool Icu::MatchTypeToName(const ICU_CONV_TYPE &type, char **name,
                          size_t *nameLen) const {
  bool result = false;
  *name = NULL;
  *nameLen = 0;

  do {
    const size_t total =
        sizeof(ICU_CONV_TYPE_NAME) / sizeof(ICU_CONV_TYPE_NAME[0]);
    size_t index = (size_t)type;
    if (index >= total)
      break;
    const char *target = ICU_CONV_TYPE_NAME[index];
    *nameLen = strlen(target);
    if (*nameLen <= 0)
      break;
    (*nameLen)++;
    *name = (char *)malloc(*nameLen);
    memcpy(*name, target, *nameLen);
    (*name)[*nameLen] = 0;
    result = true;
  } while (0);
  return result;
}
void Icu::Free(void **p) const {
  if (*p) {
    SK_FREE_PTR(*p);
    *p = nullptr;
  }
}
bool Icu::MatchNameToType(const char *name, ICU_CONV_TYPE *type) const {
  bool result = false;
  *type = ICU_CONV_TYPE::ICU_CONV_TYPE_UNKNOWN;
  do {
    if (!name)
      break;
    int i;
    for (i = 0; i < sizeof(ICU_CONV_TYPE_NAME) / sizeof(ICU_CONV_TYPE_NAME[0]);
         ++i) {
      if (0 == memcmp(name, ICU_CONV_TYPE_NAME[i], strlen(name))) {
        *type = static_cast<ICU_CONV_TYPE>(i);
        result = true;
        break;
      }
    }
  } while (0);
  return result;
}
bool Icu::DetectType(const char *src, const size_t &srcLen,
                     ICU_CONV_TYPE *type) const {
  bool result = false;
  *type = ICU_CONV_TYPE::ICU_CONV_TYPE_UNKNOWN;
  char *name = NULL;
  size_t nameLen = 0;
  do {
    if (!DetectName(src, srcLen, &name, &nameLen))
      break;
    if (!MatchNameToType(name, type))
      break;
    result = true;
  } while (0);
  SK_FREE_PTR(name);
  return result;
}
bool Icu::DetectName(const char *src, const size_t &srcLen, char **name,
                     size_t *nameLen) const {
  *name = NULL;
  *nameLen = 0;
  bool result = false;
  UErrorCode status;
  UCharsetDetector *ucs = NULL;
  do {
    if (!src || srcLen <= 0)
      break;
    status = U_ZERO_ERROR;
    ucs = ucsdet_open(&status);
    if (U_ZERO_ERROR != status || !ucs)
      break;
    status = U_ZERO_ERROR;
    ucsdet_setText(ucs, src, srcLen, &status);
    if (U_ZERO_ERROR != status)
      break;
    status = U_ZERO_ERROR;
    const UCharsetMatch *ucsm = ucsdet_detect(ucs, &status);
    if (U_ZERO_ERROR != status || !ucsm)
      break;
    status = U_ZERO_ERROR;
    const char *name_ = ucsdet_getName(ucsm, &status);
    if (U_ZERO_ERROR != status || !name_)
      break;
    size_t name_len = strlen(name_) + 1;
    *name = (char *)malloc(name_len);
    memcpy(*name, name_, strlen(name_));
    (*name)[name_len] = 0;
    *nameLen = strlen(*name);
    result = true;
  } while (0);
  if (ucs) {
    ucsdet_close(ucs);
    ucs = NULL;
  }

  return result;
}
bool Icu::Convert(const char *from_name, const char *to_name, const char *src,
                  const size_t &srcLen, char **dst, size_t *dstLen) const {
  bool result = false;
  *dst = NULL;
  *dstLen = 0;
  UErrorCode status = U_ZERO_ERROR;
  do {
    if (!from_name || !to_name || srcLen <= 0 || !src)
      break;
    *dstLen = ucnv_convert(to_name, from_name, NULL, 0, (const char *)src,
                           srcLen, &status);
    if (U_BUFFER_OVERFLOW_ERROR != status && *dstLen != 0)
      break;
    status = U_ZERO_ERROR;
    *dstLen += sizeof(char);
    *dst = (char *)malloc(*dstLen);
    ucnv_convert(to_name, from_name, (char *)(*dst), *dstLen, (const char *)src,
                 srcLen, &status);
    if (status != U_ZERO_ERROR)
      break;
    *dstLen -= sizeof(char);
    result = true;
  } while (0);
  return result;
}
bool Icu::Convert(const ICU_CONV_TYPE &from_type, const ICU_CONV_TYPE &to_type,
                  const char *src, const size_t &srcLen, char **dst,
                  size_t *dstLen) const {
  bool result = false;
  *dst = NULL;
  *dstLen = 0;
  UErrorCode status = U_ZERO_ERROR;
  char *to_name = NULL;
  char *from_name = NULL;
  size_t to_nameLen = 0;
  size_t from_nameLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    if (!MatchTypeToName(to_type, &to_name, &to_nameLen))
      break;
    if (!MatchTypeToName(from_type, &from_name, &from_nameLen))
      break;
    if (!Convert(from_name, to_name, src, srcLen, dst, dstLen))
      break;
    result = true;
  } while (0);
  SK_FREE_PTR(to_name);
  SK_FREE_PTR(from_name);
  return result;
}
bool Icu::Convert(const char *to_name, const char *src, const size_t &srcLen,
                  char **dst, size_t *dstLen) const {
  bool result = false;
  UErrorCode status = U_ZERO_ERROR;
  *dst = NULL;
  *dstLen = 0;
  char *from_name = NULL;
  size_t from_nameLen = 0;
  do {
    if (!src || srcLen <= 0 || !to_name)
      break;
    if (!DetectName(src, srcLen, &from_name, &from_nameLen))
      break;
    if (strcmp(to_name, from_name) == 0) {
      *dst = (char *)malloc(srcLen);
      *dstLen = srcLen;
      memcpy(*dst, src, *dstLen);
      status = U_ZERO_ERROR;
      result = true;
      break;
    }
    status = U_ZERO_ERROR;
    *dstLen = ucnv_convert(to_name, from_name, NULL, 0, (const char *)src,
                           srcLen, &status);
    if (U_BUFFER_OVERFLOW_ERROR != status && *dstLen != 0)
      break;
    status = U_ZERO_ERROR;
    *dstLen += sizeof(char);
    *dst = (char *)malloc(*dstLen);
    ucnv_convert(to_name, from_name, (char *)(*dst), *dstLen, (const char *)src,
                 srcLen, &status);
    if (status != U_ZERO_ERROR)
      break;
    *dstLen -= sizeof(char);
    result = true;
  } while (0);
  SK_FREE_PTR(from_name);
  return result;
}
bool Icu::Convert(const ICU_CONV_TYPE &to_type, const char *src,
                  const size_t &srcLen, char **dst, size_t *dstLen) const {
  bool result = false;
  char *to_name = NULL;
  size_t to_nameLen = 0;
  *dst = NULL;
  *dstLen = 0;
  do {
    if (!src || srcLen <= 0)
      break;
    if (!MatchTypeToName(to_type, &to_name, &to_nameLen))
      break;
    if (!Convert(to_name, src, srcLen, dst, dstLen))
      break;
    result = true;
  } while (0);
  SK_FREE_PTR(to_name);
  return result;
}
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
Icu *__gpIcu = nullptr;
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpIcu) {
      break;
    }
    __gpIcu = new Icu();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<IIcu *>(__gpIcu));
}
SHARED_API void interface_uninit() {
  SK_DELETE_PTR(__gpIcu);
}

#ifdef __cplusplus
}
#endif