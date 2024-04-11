#ifndef __PROJECTS_CURL_CURL_H_
#define __PROJECTS_CURL_CURL_H_

class Curl final : public ICurl {
public:
  Curl();
  virtual ~Curl();

private:
  void Init();
  void UnInit();

protected:
  bool Ready() const override final;
  void Release() const override final;
};

#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *, unsigned long);
SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Curl *__gpCurl;

#endif ///__PROJECTS_CURL_CURL_H_