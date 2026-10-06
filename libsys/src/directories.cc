#include <libsys/system.h>

#if defined(__OSWIN__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#endif

#include <cstdlib>

#if defined(__OSAPPLE__)
namespace libsys_detail { stl::path AppleApplicationSupportDirectory(); }
#endif
namespace {
bool Absolute(const stl::path& path) {
  if (path.empty() || !path.is_absolute()) return false;
  const auto native = path.native();
  if (native.find(decltype(native)::value_type{}) != decltype(native)::npos) return false;
  for (const auto& part : path) if (part == "." || part == "..") return false;
  return true;
}
} // namespace

stl::path ISystem::GetUserAppDataDir() { return GetUserAppDataDir({}); }

stl::path ISystem::GetUserAppDataDir(const stl::path& android_files_dir) {
#if defined(__OSANDROID__)
  // Native code cannot derive the correct Android user/profile or credential
  // protected app container from a package name. The app supplies filesDir.
  if (!Absolute(android_files_dir)) return {};
  std::error_code error;
  const auto result = std::filesystem::canonical(android_files_dir, error);
  return !error && Absolute(result) ? result : stl::path{};
#else
  if (!android_files_dir.empty()) return {};
  stl::path result;
#if defined(__OSWIN__)
  PWSTR value = nullptr;
  const auto status = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &value);
  if (SUCCEEDED(status) && value) result = value;
  if (value) CoTaskMemFree(value);
#elif defined(__OSAPPLE__)
  result = libsys_detail::AppleApplicationSupportDirectory();
#elif defined(__OSLINUX__)
  const char* xdg = std::getenv("XDG_DATA_HOME");
  if (xdg && Absolute(stl::path(xdg))) result = xdg;
  else {
    const char* home = std::getenv("HOME");
    if (home && Absolute(stl::path(home))) result = stl::path(home) / ".local" / "share";
  }
#endif
  return Absolute(result) ? result : stl::path{};
#endif
}
