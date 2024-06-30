#ifndef __OSSL_H_
#define __OSSL_H_

#include "stl.h"

#define OSSL_ENABLE_OPENSSL_3 0

namespace ossl {
class Ssl {
public:
  static std::string MD5(const std::string &, const bool &bin2hex = true);
  static std::string Base64Encode(const std::string &,
                                  const bool &multiline = false);
  static std::string Base64Decode(const std::string &,
                                  const bool &multiline = false);

  static std::string SHA256(const std::string &, const bool &bin2hex = true);
  static std::string HMAC_SHA1(const std::string &src, const std::string &seed,
                               const bool &bin2hex = true);
  static std::string HMAC_SHA256(const std::string &src,
                                 const std::string &seed,
                                 const bool &bin2hex = true);
};
} // namespace ossl
#endif //__OSSL_H_