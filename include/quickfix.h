#ifndef __QUICKFIX_H_
#define __QUICKFIX_H_

#include "interface.h"

namespace fix {
enum class ConnectionType : unsigned int {
  CONNECTION_TYPE_ACCEPTOR = 0,
  CONNECTION_TYPE_INITIATOR,
};
class IFix : public Interface<IFix> {
public:
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
  virtual bool Start(const char *settings) = 0;
  virtual void Stop() = 0;
};
} // namespace fix

#endif ///__QUICKFIX_H_