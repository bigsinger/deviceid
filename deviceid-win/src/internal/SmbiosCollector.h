#pragma once

#include <cstdint>
#include <string>

namespace DeviceInfoSDK {
namespace internal {

struct SmbiosResult {
    bool ok = false;
    std::string brand;
    std::string model;
    std::uint32_t native_error = 0;
};

SmbiosResult CollectSmbiosSystemInfo() noexcept;

} // namespace internal
} // namespace DeviceInfoSDK
