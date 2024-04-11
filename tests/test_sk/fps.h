#ifdef __SK_FPS_H_
#endif __SK_FPS_H_

namespace sk {

enum SK_RES {
  SK_OK = 0,
  SK_NO = 1,
};

namespace fps {

SKFPS_RES fps_base64_encode(const char *src, size_t srcLen, char **dst,
                            size_t *dstLen);
SKFPS_RES fps_base64_decode(const char *src, size_t srcLen, char **dst,
                            size_t *dstLen);

extern const char *default_script_js_inject;
extern const char *default_extensions_manifest;
} // namespace fps

namespace extensions {
namespace proxy {} // namespace proxy
} // namespace extensions
} // namespace sk

#endif