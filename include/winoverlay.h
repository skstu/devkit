#ifndef __WINOVERLAY_H_
#define __WINOVERLAY_H_

#include "interface.h"

namespace win {
class IOverlay : public Interface<IOverlay> {
public:
  using tfStreamCb = std::function<void(const char *, const size_t &)>;

public:
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
  virtual void AppendPos(const long &, const long &, const long &,
                         const long &) = 0;
  virtual bool Screenshot(const tfStreamCb &) const = 0;
};
} // namespace win

#endif //__WINOVERLAY_H_