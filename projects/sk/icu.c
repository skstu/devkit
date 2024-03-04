#include "stdafx.h"
#include <unicode/uclean.h>
#include <unicode/ustdio.h>
#include <unicode/platform.h>
#include <unicode/putil.h>
#include <unicode/ucnv.h>
#include <unicode/uconfig.h>
#include <unicode/ucsdet.h>
#include <unicode/udata.h>
#include <unicode/unistr.h>
#include <unicode/ustring.h>
#include <unicode/utf16.h>
#include <unicode/utf32.h>
#include <unicode/utf8.h>
#include <unicode/utypes.h>

static const char ICU_CONV_TYPE_NAME[][100] = {
    "UNKNOWN",  "UTF-8",    "UTF-16", "UTF-16LE", "UTF-16BE", "UTF-32",
    "UTF-32LE", "UTF-32BE", "ASCII",  "GBK",      "GB18030",
};

static sk_icu_conv_type icu_match_name_to_type(const char *name) {
  sk_icu_conv_type r = ICU_CONV_TYPE_UNKNOWN;
  do {
    if (!name)
      break;
    int i;
    for (i = 0; i < sizeof(ICU_CONV_TYPE_NAME) / sizeof(ICU_CONV_TYPE_NAME[0]);
         ++i) {
      if (0 == sk__memcmp(name, ICU_CONV_TYPE_NAME[i], strlen(name))) {
        r = i;
        break;
      }
    }
  } while (0);
  return r;
}
int sk_icu_detect_name(const char *textIn, size_t len, char **name) {
  int r = SK_NO;
  UErrorCode status;
  UCharsetDetector *ucs = NULL;
  *name = NULL;
  do {
    if (!textIn || len <= 0)
      break;
    status = U_ZERO_ERROR;
    ucs = ucsdet_open(&status);
    if (U_ZERO_ERROR != status || !ucs)
      break;
    status = U_ZERO_ERROR;
    ucsdet_setText(ucs, textIn, len, &status);
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
    *name = sk__malloc(name_len);
    memcpy(*name, name_, strlen(name_));
    (*name)[name_len] = 0;
    r = SK_OK;
  } while (0);
  if (ucs) {
    ucsdet_close(ucs);
    ucs = NULL;
  }
  return r;
}

int sk_icu_detect_type(const char *textIn, size_t len, sk_icu_conv_type *type) {
  int r = SK_NO;
  char *name = NULL;
  r = sk_icu_detect_name(textIn, len, &name);
  if (SK_OK == r) {
    *type = icu_match_name_to_type(name);
  }
  return r;
}