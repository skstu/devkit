#include <libsys.h>
#include "../gpu_info.hpp"

#include <dirent.h>
#include <fstream>
#include <map>

namespace {

std::string ReadFirstLine(const std::string &path) {
  std::ifstream input(path);
  std::string line;
  if (!std::getline(input, line))
    return {};
  const auto begin = line.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos)
    return {};
  const auto end = line.find_last_not_of(" \t\r\n");
  return line.substr(begin, end - begin + 1);
}

uint32_t ReadHexValue(const std::string &path) {
  const std::string value = ReadFirstLine(path);
  if (value.empty())
    return 0;
  try {
    return static_cast<uint32_t>(std::stoul(value, nullptr, 0));
  } catch (...) {
    return 0;
  }
}

std::map<std::string, std::string> ReadUEvent(const std::string &path) {
  std::map<std::string, std::string> values;
  std::ifstream input(path);
  std::string line;
  while (std::getline(input, line)) {
    const auto separator = line.find('=');
    if (separator != std::string::npos)
      values.emplace(line.substr(0, separator), line.substr(separator + 1));
  }
  return values;
}

bool IsDRMCardName(const std::string &name) {
  if (name.rfind("card", 0) != 0 || name.size() == 4)
    return false;
  return std::all_of(name.begin() + 4, name.end(),
                     [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

int DRMCardIndex(const std::string &name) {
  int result = 0;
  for (auto it = name.begin() + 4; it != name.end(); ++it)
    result = result * 10 + (*it - '0');
  return result;
}

} // namespace

std::vector<ISystem::GPUInfo> ISystem::GetGPUInfo() {
  std::vector<std::string> card_names;
  DIR *directory = opendir("/sys/class/drm");
  if (!directory)
    return {};

  while (dirent *entry = readdir(directory)) {
    const std::string name = entry->d_name;
    if (IsDRMCardName(name))
      card_names.push_back(name);
  }
  closedir(directory);
  std::sort(card_names.begin(), card_names.end(),
            [](const std::string &left, const std::string &right) {
              return DRMCardIndex(left) < DRMCardIndex(right);
            });

  std::vector<GPUInfo> devices;
  for (const auto &card : card_names) {
    const std::string base = "/sys/class/drm/" + card + "/device/";
    const auto uevent = ReadUEvent(base + "uevent");
    const auto driver_it = uevent.find("DRIVER");
    const std::string driver =
        driver_it == uevent.end() ? std::string{} : driver_it->second;

    GPUInfo info;
    info.name = driver.empty() ? card : driver;
    info.vendor_id = ReadHexValue(base + "vendor");
    info.device_id = ReadHexValue(base + "device");
    info.vendor =
        libsys::detail::ClassifyGPUVendor(info.vendor_id, info.name, driver);
    info.primary = ReadFirstLine(base + "boot_vga") == "1";
    devices.push_back(std::move(info));
  }

  if (!devices.empty() &&
      std::none_of(devices.begin(), devices.end(),
                   [](const GPUInfo &device) { return device.primary; }))
    devices.front().primary = true;
  return devices;
}
