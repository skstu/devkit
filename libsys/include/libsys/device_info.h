#ifndef DEVKIT_DEVICE_INFO_H
#define DEVKIT_DEVICE_INFO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Presentation facts only: never a serial, hardware UUID, MAC or fingerprint.
 * Unknown form is a valid result. Strings are UTF-8 and NUL terminated.
 * Set struct_size and version=1. No permissions, network or host context needed.
 * Returns 0 on success, -1 for an invalid output contract. */
typedef struct dksys_device_info {
  uint32_t struct_size;
  uint32_t version;
  char platform[16];
  char form[24];
  char model[128];
  char source[24];
} dksys_device_info;
int dksys_device_detect(dksys_device_info *output);
#ifdef __cplusplus
}
#endif
#endif
