#ifndef __ICU_HPP_
#define __ICU_HPP_

#include "interface.h"

namespace icu {
typedef enum {
  ICU_CONV_TYPE_UNKNOWN = 0,
  ICU_CONV_TYPE_UTF8,
  ICU_CONV_TYPE_UTF16,
  ICU_CONV_TYPE_UTF16LE,
  ICU_CONV_TYPE_UTF16BE,
  ICU_CONV_TYPE_UTF32,
  ICU_CONV_TYPE_UTF32LE,
  ICU_CONV_TYPE_UTF32BE,
  ICU_CONV_TYPE_ASCII,
  ICU_CONV_TYPE_GBK,
  ICU_CONV_TYPE_GB18030,
} ICU_CONV_TYPE;

class IIcu : public Interface<IIcu> {
public:
  virtual void Free(void **) const = 0;
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
  virtual bool DetectType(const char *src, const size_t &srcLen,
                          ICU_CONV_TYPE *) const = 0;
  virtual bool DetectName(const char *src, const size_t &srcLen, char **name,
                          size_t *nameLen) const = 0;
  virtual bool MatchTypeToName(const ICU_CONV_TYPE &type, char **name,
                               size_t *nameLen) const = 0;
  virtual bool MatchNameToType(const char *name, ICU_CONV_TYPE *type) const = 0;
  virtual bool Convert(const char *from, const char *to, const char *formSrc,
                       const size_t &formSrcLen, char **toDst,
                       size_t *toDstLen) const = 0;
  virtual bool Convert(const ICU_CONV_TYPE &from, const ICU_CONV_TYPE &to,
                       const char *formSrc, const size_t &formSrcLen,
                       char **toDst, size_t *toDstLen) const = 0;
  virtual bool Convert(const char *to, const char *formSrc,
                       const size_t &formSrcLen, char **toDst,
                       size_t *toDstLen) const = 0;
  virtual bool Convert(const ICU_CONV_TYPE &to, const char *formSrc,
                       const size_t &formSrcLen, char **toDst,
                       size_t *toDstLen) const = 0;
};
} // namespace icu

#endif //__ICU_HPP_