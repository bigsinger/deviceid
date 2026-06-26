#pragma once

#include <cstdint>
#include <string>

namespace DeviceInfoSDK {
namespace internal {

struct NetworkResult {
    bool ok = false;
    bool has_candidate = false;
    std::string network_type = "unknown";
    std::string mac;
    std::uint32_t native_error = 0;
    std::uint64_t diagnostic_flags = 0;
};

NetworkResult CollectNetwork(bool collect_mac) noexcept;

} // namespace internal
} // namespace DeviceInfoSDK
