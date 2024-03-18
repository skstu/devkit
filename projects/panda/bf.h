#ifndef __PROJECTS_PANDA_BF_H_
#define __PROJECTS_PANDA_BF_H_

class BrowserFingerprint final : public IBrowserFingerprint {
public:
  BrowserFingerprint();
  ~BrowserFingerprint();

protected:
  void Release() const override final;
  bool Location() override final;
  bool IP_address() override final;
  bool WebRTC_address() override final;
  bool IP_location() override final;
  bool Timezone_from_Javascript() override final;
  bool Time_from_Javascript() override final;
  bool Time_from_IP() override final;
  bool Daylight_savings_time() override final;
  bool Languages_from_Javascript() override final;
  bool Accept_Language_header() override final;
  bool Internationalization_API() override final;
  bool User_Agent_HTTP() override final;
  bool User_Agent_Javascript() override final;
  bool Platform() override final;
  bool DoNotTrack() override final;
  bool HardwareConcurency() override final;
  bool Screen_resolution() override final;
  bool Available_screen_size() override final;
  bool Canvas_hash() override final;
  bool WebGL_hash() override final;
  bool WebGL_unmasked_vendor() override final;
  bool WebGL_unmasked_renderer() override final;
  bool WebGL_version() override final;
  bool AudioContext_hash() override final;
  bool Font_hash() override final;
  bool Fonts() override final;
  void OutputXml(char **, size_t *) const override final;
  void OutputJson(char **, size_t *) const override final;
  bool InputXml(const char *, size_t) override final;
  bool InputJson(const char *, size_t) override final;
};

#endif ///__PROJECTS_PANDA_BF_H_