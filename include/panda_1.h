#ifndef __PANDA_H_
#define __PANDA_H_

#include "interface.h"

namespace panda {
class IPanda : public Interface<IPanda> {
public:
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
  virtual void Test() const = 0;
};
} // namespace panda

#endif ///__PANDA_H_