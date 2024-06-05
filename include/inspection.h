#ifndef __INSPECTION_H_
#define __INSPECTION_H_

#include "interface.h"

namespace inspect {
class Inspection : public Interface<Inspection> {
public:
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
};
} // namespace inspect

#endif //__INSPECTION_H_