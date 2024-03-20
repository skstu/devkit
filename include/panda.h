#ifndef __PANDA_H_
#define __PANDA_H_

#include "interface.h"

namespace panda {
class IBrowserExtensions {
public:
  virtual void Release() const = 0;
};
class IBrowserFingerprint {
public:
  virtual void Release() const = 0;
  virtual bool Location() = 0;
  virtual bool IP_address() = 0;
  virtual bool WebRTC_address() = 0;
  virtual bool IP_location() = 0;
  virtual bool Timezone_from_Javascript() = 0;
  virtual bool Time_from_Javascript() = 0;
  virtual bool Time_from_IP() = 0;
  virtual bool Daylight_savings_time() = 0;
  virtual bool Languages_from_Javascript() = 0;
  virtual bool Accept_Language_header() = 0;
  virtual bool Internationalization_API() = 0;
  virtual bool User_Agent_HTTP() = 0;
  virtual bool User_Agent_Javascript() = 0;
  virtual bool Platform() = 0;
  virtual bool DoNotTrack() = 0;
  virtual bool HardwareConcurency() = 0;
  virtual bool Screen_resolution() = 0;
  virtual bool Available_screen_size() = 0;
  virtual bool Canvas_hash() = 0;
  virtual bool WebGL_hash() = 0;
  virtual bool WebGL_unmasked_vendor() = 0;
  virtual bool WebGL_unmasked_renderer() = 0;
  virtual bool WebGL_version() = 0;
  virtual bool AudioContext_hash() = 0;
  virtual bool Font_hash() = 0;
  virtual bool Fonts() = 0;
  virtual void OutputJson(char **, size_t *) const = 0;
  virtual bool InputJson(const char *, size_t) = 0;
};
class IPanda : public Interface<IPanda> {
public:
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
  virtual IBrowserFingerprint *CreateBF() const = 0;
  virtual IBrowserExtensions *CreateEXT() const = 0;
  virtual void Test() const = 0;
};
} // namespace panda

#endif ///__PANDA_H_