#ifndef __PROJECTS_INSPECTION_OVERLAY_CONFIG_H_
#define __PROJECTS_INSPECTION_OVERLAY_CONFIG_H_

class Config final : public IOverlay::IConfig {
public:
  Config();
  virtual ~Config();
  void Release() const;

protected:
  void EnableScreenshot(const bool &) override final;
  void EnableDrawBorder(const bool &) override final;
  void SetBorderARGB(const unsigned short &a, const unsigned short &r,
                     const unsigned short &g,
                     const unsigned short &b) override final;
  void SetLabel(const wchar_t *) override final;

public:
  std::wstring GetLabel() const;
  bool EnableScreenshot() const;
  bool EnableDrawBorder() const;
  unsigned short BorderColorA() const;
  unsigned short BorderColorR() const;
  unsigned short BorderColorG() const;
  unsigned short BorderColorB() const;

private:
  std::wstring label_;
  unsigned short border_color_a = 0;
  unsigned short border_color_r = 0;
  unsigned short border_color_g = 0;
  unsigned short border_color_b = 0;
  std::atomic_bool enable_screenshot_ = false;
  std::atomic_bool enable_draw_border_ = true;
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
};
#endif //__PROJECTS_INSPECTION_OVERLAY_CONFIG_H_