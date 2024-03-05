#ifndef __QUICKFIX_H_
#define __QUICKFIX_H_

#include "interface.h"

namespace fix {
class IFix : public Interface<IFix> {
public:
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
};
} // namespace fix

#endif ///__QUICKFIX_H_