#ifndef __LIBCURL_H_
#define __LIBCURL_H_

#include "interface.h"

namespace curl {
class ICurl : public Interface<ICurl> {
public:
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
};
} // namespace curl

#endif ///__LIBCURL_H_
