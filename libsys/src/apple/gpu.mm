#include <libsys.h>
#include "../gpu_info.hpp"

#import <Metal/Metal.h>

namespace {

std::string MetalDeviceName(id<MTLDevice> device) {
  if (!device || !device.name)
    return {};
  const char *name = device.name.UTF8String;
  return name ? std::string(name) : std::string{};
}

} // namespace

std::vector<ISystem::GPUInfo> ISystem::GetGPUInfo() {
  std::vector<GPUInfo> devices;
  @autoreleasepool {
    id<MTLDevice> default_device = MTLCreateSystemDefaultDevice();
    NSArray<id<MTLDevice>> *metal_devices = MTLCopyAllDevices();
    for (id<MTLDevice> device in metal_devices) {
      GPUInfo info;
      info.name = MetalDeviceName(device);
      info.vendor = libsys::detail::ClassifyGPUVendor(0, info.name);
      info.primary = device == default_device;
      devices.push_back(std::move(info));
    }

    if (devices.empty() && default_device) {
      GPUInfo info;
      info.name = MetalDeviceName(default_device);
      info.vendor = libsys::detail::ClassifyGPUVendor(0, info.name);
      info.primary = true;
      devices.push_back(std::move(info));
    }
  }

  if (!devices.empty() &&
      std::none_of(devices.begin(), devices.end(),
                   [](const GPUInfo &device) { return device.primary; }))
    devices.front().primary = true;
  return devices;
}
