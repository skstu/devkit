#ifndef __PROJECTS_ICU_ICU_H_
#define __PROJECTS_ICU_ICU_H_

class Icu final : public IIcu {
public:
  Icu();
  virtual ~Icu();

private:
  void Init();
  void UnInit();

protected:
  void Free(void **) const override final;
  bool Ready() const override final;
  void Release() const override final;
  bool Convert(const char *from, const char *to, const char *formSrc,
               const size_t &formSrcLen, char **toDst,
               size_t *toDstLen) const override final;
  bool Convert(const ICU_CONV_TYPE &from, const ICU_CONV_TYPE &to,
               const char *formSrc, const size_t &formSrcLen, char **toDst,
               size_t *toDstLen) const override final;
  bool Convert(const char *to, const char *formSrc, const size_t &formSrcLen,
               char **toDst, size_t *toDstLen) const override final;
  bool Convert(const ICU_CONV_TYPE &to, const char *formSrc,
               const size_t &formSrcLen, char **toDst,
               size_t *toDstLen) const override final;
  bool DetectName(const char *src, const size_t &srcLen, char **name,
                  size_t *nameLen) const override final;
  bool DetectType(const char *src, const size_t &srcLen,
                  ICU_CONV_TYPE *) const override final;
  bool MatchTypeToName(const ICU_CONV_TYPE &type, char **name,
                       size_t *nameLen) const override final;
  bool MatchNameToType(const char *name,
                       ICU_CONV_TYPE *type) const override final;
};

#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *, unsigned long);
SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Icu *__gpIcu;

#endif ///__PROJECTS_ICU_ICU_H_