#pragma once
#include <cstdint>
#include <optional>
namespace libsys {
    struct SystemResources {
        std::uint64_t available_memory = 0;
        std::optional<std::uint64_t> descriptor_limit, descriptors_used;
    };
    std::uint64_t AvailableMemory();
    SystemResources InspectSystemResources();
}
