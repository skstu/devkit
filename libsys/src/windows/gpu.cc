#include <libsys.h>
#include "../gpu_info.hpp"

#include <dxgi1_2.h>
#include <setupapi.h>

namespace {

using GPUInfo = ISystem::GPUInfo;

constexpr GUID kDisplayDeviceClass = {
    0x4d36e968,
    0xe325,
    0x11ce,
    {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

std::string WideToUTF8(const wchar_t *value) {
  if (!value || !*value)
    return {};
  const int size =
      WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1)
    return {};
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr,
                      nullptr);
  result.pop_back();
  return result;
}

uint32_t ParseHardwareIdComponent(const std::wstring &hardware_id,
                                  const wchar_t *key) {
  const auto position = hardware_id.find(key);
  if (position == std::wstring::npos)
    return 0;
  const wchar_t *begin = hardware_id.c_str() + position + std::wcslen(key);
  wchar_t *end = nullptr;
  const unsigned long value = std::wcstoul(begin, &end, 16);
  return end == begin ? 0 : static_cast<uint32_t>(value);
}

std::wstring ReadDeviceProperty(HDEVINFO devices, SP_DEVINFO_DATA &device,
                                DWORD property) {
  DWORD required = 0;
  DWORD type = 0;
  SetupDiGetDeviceRegistryPropertyW(devices, &device, property, &type, nullptr,
                                    0, &required);
  if (!required || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return {};

  std::vector<BYTE> buffer(required + sizeof(wchar_t), 0);
  if (!SetupDiGetDeviceRegistryPropertyW(
          devices, &device, property, &type, buffer.data(),
          static_cast<DWORD>(buffer.size()), nullptr))
    return {};
  return reinterpret_cast<const wchar_t *>(buffer.data());
}

struct AdapterIdentity {
  uint32_t vendor_id = 0;
  uint32_t device_id = 0;
};

AdapterIdentity PrimaryDisplayIdentity() {
  for (DWORD index = 0;; ++index) {
    DISPLAY_DEVICEW display{};
    display.cb = sizeof(display);
    if (!EnumDisplayDevicesW(nullptr, index, &display, 0))
      break;
    if (!(display.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
      continue;

    const std::wstring hardware_id = display.DeviceID;
    return {ParseHardwareIdComponent(hardware_id, L"VEN_"),
            ParseHardwareIdComponent(hardware_id, L"DEV_")};
  }
  return {};
}

bool IsSoftwareAdapter(uint32_t vendor_id, const std::string &name,
                       const std::wstring &hardware_id) {
  const std::string normalized = libsys::detail::NormalizeGPUText(name);
  return vendor_id == 0x1414 ||
         normalized.find("microsoft basic render") != std::string::npos ||
         normalized.find("remote display") != std::string::npos ||
         normalized.find("indirect display") != std::string::npos ||
         (hardware_id.rfind(L"ROOT\\", 0) == 0 && vendor_id == 0);
}

std::vector<GPUInfo> EnumerateDXGIAdapters() {
  std::vector<GPUInfo> devices;
  IDXGIFactory1 *factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                reinterpret_cast<void **>(&factory))))
    return devices;

  for (UINT index = 0;; ++index) {
    IDXGIAdapter1 *adapter = nullptr;
    const HRESULT result = factory->EnumAdapters1(index, &adapter);
    if (result == DXGI_ERROR_NOT_FOUND)
      break;
    if (FAILED(result)) {
      if (adapter)
        adapter->Release();
      continue;
    }

    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
        !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
      GPUInfo info;
      info.name = WideToUTF8(desc.Description);
      info.vendor_id = desc.VendorId;
      info.device_id = desc.DeviceId;
      info.vendor =
          libsys::detail::ClassifyGPUVendor(info.vendor_id, info.name);
      info.primary = devices.empty();
      devices.push_back(std::move(info));
    }
    adapter->Release();
  }

  factory->Release();
  return devices;
}

std::vector<GPUInfo> EnumerateSetupAPIAdapters() {
  std::vector<GPUInfo> devices;
  HDEVINFO device_set = SetupDiGetClassDevsW(&kDisplayDeviceClass, nullptr,
                                             nullptr, DIGCF_PRESENT);
  if (device_set == INVALID_HANDLE_VALUE)
    return devices;

  const AdapterIdentity primary = PrimaryDisplayIdentity();
  for (DWORD index = 0;; ++index) {
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    if (!SetupDiEnumDeviceInfo(device_set, index, &device)) {
      if (GetLastError() == ERROR_NO_MORE_ITEMS)
        break;
      continue;
    }

    std::wstring name =
        ReadDeviceProperty(device_set, device, SPDRP_FRIENDLYNAME);
    if (name.empty())
      name = ReadDeviceProperty(device_set, device, SPDRP_DEVICEDESC);
    const std::wstring hardware_id =
        ReadDeviceProperty(device_set, device, SPDRP_HARDWAREID);

    GPUInfo info;
    info.name = WideToUTF8(name.c_str());
    info.vendor_id = ParseHardwareIdComponent(hardware_id, L"VEN_");
    info.device_id = ParseHardwareIdComponent(hardware_id, L"DEV_");
    if (IsSoftwareAdapter(info.vendor_id, info.name, hardware_id))
      continue;
    info.vendor = libsys::detail::ClassifyGPUVendor(info.vendor_id, info.name);
    info.primary = primary.vendor_id != 0 &&
                   info.vendor_id == primary.vendor_id &&
                   info.device_id == primary.device_id;
    devices.push_back(std::move(info));
  }
  SetupDiDestroyDeviceInfoList(device_set);

  if (!devices.empty() &&
      std::none_of(devices.begin(), devices.end(),
                   [](const GPUInfo &device) { return device.primary; }))
    devices.front().primary = true;
  return devices;
}

} // namespace

std::vector<ISystem::GPUInfo> ISystem::GetGPUInfo() {
  auto devices = EnumerateSetupAPIAdapters();
  return devices.empty() ? EnumerateDXGIAdapters() : devices;
}
