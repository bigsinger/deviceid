#pragma once

#include "DeviceInfoSDK/DeviceInfo.h"

#include <cstdint>
#include <string>

namespace DeviceInfoSDK {
namespace internal {

struct DeviceIdOutcome {
    std::string id;
    ResultCode code = ResultCode::kInternalError;
    std::uint64_t diagnostic_flags = 0;
    std::uint32_t native_error = 0;
};

bool IsValidNamespace(const std::string& value) noexcept;
bool IsValidUuidV4Text(const std::string& text, std::string* normalized) noexcept;

DeviceIdOutcome GetOrCreateDeviceId(const DeviceInfoOptions& options) noexcept;
ResultCode DeletePersistedDeviceIdInternal(
    const DeviceInfoOptions& options,
    std::uint32_t* native_error) noexcept;

} // namespace internal
} // namespace DeviceInfoSDK
