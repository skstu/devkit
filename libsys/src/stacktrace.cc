#include <libsys.hpp>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <mutex>
#include <sstream>

#if defined(__OSWIN__)
#include <DbgHelp.h>
#elif defined(__OSLINUX__) || defined(__OSMAC__)
#include <cxxabi.h>
#include <cstdlib>
#include <dlfcn.h>
#include <execinfo.h>
#endif

namespace {
#if defined(__OSWIN__) || defined(__OSLINUX__) || defined(__OSMAC__)
constexpr std::size_t kInternalSkipFrames = 1;
#endif
constexpr std::size_t kMaxCaptureFrames = 256;

#if defined(__OSWIN__) || defined(__OSLINUX__) || defined(__OSMAC__)
std::string FormatAddress(const void *addr) {
  std::ostringstream oss;
  oss << "0x" << std::hex << std::uppercase
      << reinterpret_cast<std::uintptr_t>(addr);
  return oss.str();
}
#endif

#if defined(__OSWIN__)
std::string FormatWindowsStackFrame(HANDLE process, void *frame) {
  DWORD64 addr = reinterpret_cast<DWORD64>(frame);
  constexpr std::size_t symbolBufferSize = sizeof(SYMBOL_INFO) + MAX_SYM_NAME;
  std::vector<char> symbolBuffer(symbolBufferSize, 0);
  auto *symbol = reinterpret_cast<SYMBOL_INFO *>(symbolBuffer.data());
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;

  std::ostringstream oss;
  DWORD64 displacement = 0;
  if (SymFromAddr(process, addr, &displacement, symbol)) {
    oss << symbol->Name;
    if (displacement)
      oss << " + 0x" << std::hex << std::uppercase << displacement;
  } else {
    oss << FormatAddress(frame);
  }

  IMAGEHLP_LINE64 line{};
  line.SizeOfStruct = sizeof(line);
  DWORD lineDisplacement = 0;
  if (SymGetLineFromAddr64(process, addr, &lineDisplacement, &line)) {
    oss << " (" << line.FileName << ":" << std::dec << line.LineNumber << ")";
  }
  return oss.str();
}
#elif defined(__OSLINUX__) || defined(__OSMAC__)
std::string DemangleSymbol(const char *name) {
  if (!name || !*name)
    return {};

  int status = 0;
  char *demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
  if (status != 0 || !demangled)
    return name;

  std::string result(demangled);
  std::free(demangled);
  return result;
}

std::string FormatPosixStackFrame(void *frame) {
  Dl_info info{};
  if (!dladdr(frame, &info))
    return FormatAddress(frame);

  std::ostringstream oss;
  const std::string symbol = DemangleSymbol(info.dli_sname);
  if (!symbol.empty()) {
    oss << symbol;
    if (info.dli_saddr) {
      const auto offset =
          reinterpret_cast<std::uintptr_t>(frame) -
          reinterpret_cast<std::uintptr_t>(info.dli_saddr);
      if (offset)
        oss << " + 0x" << std::hex << std::uppercase << offset;
    }
  } else if (info.dli_fname && *info.dli_fname) {
    oss << info.dli_fname << "!" << FormatAddress(frame);
  } else {
    oss << FormatAddress(frame);
  }

  if (info.dli_fname && *info.dli_fname)
    oss << " (" << info.dli_fname << ")";
  return oss.str();
}
#endif
} // namespace

std::vector<std::string> ISystem::CaptureStackTrace(std::size_t max_frames,
                                                    std::size_t skip_frames) {
  std::vector<std::string> result;
  if (max_frames == 0)
    return result;

  max_frames = std::min(max_frames, kMaxCaptureFrames);
  skip_frames = std::min(skip_frames, kMaxCaptureFrames);
#if defined(__OSWIN__)
  const std::size_t skip = skip_frames + kInternalSkipFrames;
  const std::size_t capture_limit =
      std::min(max_frames + skip, kMaxCaptureFrames);
  std::vector<void *> frames(capture_limit, nullptr);
  const USHORT captured = CaptureStackBackTrace(
      0, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
  if (captured <= skip)
    return result;

  static std::mutex symbolMutex;
  static bool initialized = false;
  HANDLE process = GetCurrentProcess();
  std::lock_guard<std::mutex> lk(symbolMutex);
  if (!initialized) {
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    initialized = SymInitialize(process, nullptr, TRUE) == TRUE ||
                  GetLastError() == ERROR_INVALID_PARAMETER;
  }
  if (!initialized)
    return result;

  result.reserve(std::min<std::size_t>(max_frames, captured - skip));
  for (USHORT i = static_cast<USHORT>(skip); i < captured &&
                                                result.size() < max_frames;
       ++i) {
    if (frames[i])
      result.emplace_back(FormatWindowsStackFrame(process, frames[i]));
  }
#elif defined(__OSLINUX__) || defined(__OSMAC__)
  const std::size_t skip = skip_frames + kInternalSkipFrames;
  const std::size_t capture_limit =
      std::min(max_frames + skip, kMaxCaptureFrames);
  std::vector<void *> frames(capture_limit, nullptr);
  const int captured = backtrace(frames.data(), static_cast<int>(frames.size()));
  if (captured <= static_cast<int>(skip))
    return result;

  result.reserve(
      std::min<std::size_t>(max_frames, static_cast<std::size_t>(captured) - skip));
  for (int i = static_cast<int>(skip);
       i < captured && result.size() < max_frames; ++i) {
    if (frames[static_cast<std::size_t>(i)])
      result.emplace_back(FormatPosixStackFrame(frames[static_cast<std::size_t>(i)]));
  }
#endif

  return result;
}
