#ifndef __SK_FPS_H_
#define __SK_FPS_H_

class SKFps {
public:
  SKFps();
  ~SKFps();

private:
  void Init();
  void UnInit();

public:
  void SetConfigure(const std::string &);

private:
  std::string inject_js_code_template_;
  std::string inject_configure_;

private:
  std::string skfps_js_script_part_screen__configure_;
};

extern const char skfps_js_injector_code_template[];
extern const char skfps_js_injector_code_template_bak[];
#endif