#if !defined(__LIBSYS_GPU_INFO_HPP__)
#define __LIBSYS_GPU_INFO_HPP__

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace libsys::detail {

inline std::string NormalizeGPUText(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   if (std::isalnum(ch))
                     return static_cast<char>(std::tolower(ch));
                   return ' ';
                 });
  return value;
}

inline bool HasGPUWord(const std::string &value, const std::string &word) {
  const std::string padded = " " + value + " ";
  return padded.find(" " + word + " ") != std::string::npos;
}

inline ISystem::GPUVendor ClassifyGPUVendor(uint32_t vendor_id,
                                            const std::string &name,
                                            const std::string &driver = {}) {
  switch (vendor_id) {
  case 0x1002:
    return ISystem::GPUVendor::AMD;
  case 0x106b:
    return ISystem::GPUVendor::Apple;
  case 0x8086:
    return ISystem::GPUVendor::Intel;
  case 0x10de:
    return ISystem::GPUVendor::NVIDIA;
  case 0x17cb:
  case 0x5143:
    return ISystem::GPUVendor::Qualcomm;
  default:
    break;
  }

  const std::string value = NormalizeGPUText(name + " " + driver);
  if (value.find("nvidia") != std::string::npos ||
      value.find("geforce") != std::string::npos ||
      value.find("quadro") != std::string::npos || HasGPUWord(value, "nouveau"))
    return ISystem::GPUVendor::NVIDIA;
  if (value.find("advanced micro devices") != std::string::npos ||
      value.find("radeon") != std::string::npos ||
      value.find("amdgpu") != std::string::npos || HasGPUWord(value, "amd") ||
      HasGPUWord(value, "ati"))
    return ISystem::GPUVendor::AMD;
  if (value.find("qualcomm") != std::string::npos ||
      value.find("adreno") != std::string::npos || HasGPUWord(value, "msm") ||
      HasGPUWord(value, "kgsl") || HasGPUWord(value, "freedreno"))
    return ISystem::GPUVendor::Qualcomm;
  if (value.find("intel") != std::string::npos || HasGPUWord(value, "i915") ||
      HasGPUWord(value, "xe"))
    return ISystem::GPUVendor::Intel;
  if (value.find("apple") != std::string::npos || HasGPUWord(value, "asahi") ||
      HasGPUWord(value, "agx"))
    return ISystem::GPUVendor::Apple;
  return ISystem::GPUVendor::Unknown;
}

} // namespace libsys::detail

#endif // __LIBSYS_GPU_INFO_HPP__
