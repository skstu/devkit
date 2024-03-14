#ifndef __SK_VERSION_H_
#define __SK_VERSION_H_

#define SK_VERSION_MAJOR 1
#define SK_VERSION_MINOR 0
#define SK_VERSION_PATCH 0
#define SK_VERSION_IS_RELEASE 0
#define SK_VERSION_SUFFIX "dev"

#define SK_VERSION_HEX                                                         \
  ((SK_VERSION_MAJOR << 16) | (SK_VERSION_MINOR << 8) | (SK_VERSION_PATCH))

#endif /* __SK_VERSION_H_ */