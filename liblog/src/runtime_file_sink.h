#pragma once

#include "liblog/runtime_logger.h"
#include <spdlog/sinks/sink.h>

namespace sovkit::log::runtime_detail {

// Error codes are fixed literals, never system exception text or paths.
struct FileFailure {
  const char *code;
};
spdlog::sink_ptr MakeFileSink(const RuntimeConfig &config,
                              const std::string &session);
std::uint64_t ProcessId() noexcept;
std::string UtcTimestamp(bool filename = false);

} // namespace sovkit::log::runtime_detail
