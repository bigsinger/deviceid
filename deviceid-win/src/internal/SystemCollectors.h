#pragma once

#include <cstdint>
#include <string>

namespace DeviceInfoSDK {
namespace internal {

struct StringResult {
    bool ok = false;
    std::string value;
    std::uint32_t native_error = 0;
    std::uint64_t diagnostic_flags = 0;
};

struct UInt32Result {
    bool ok = false;
    std::uint32_t value = 0;
    std::uint32_t native_error = 0;
};

struct UInt64Result {
    bool ok = false;
    std::uint64_t value = 0;
    std::uint32_t native_error = 0;
};

struct DisplayResult {
    bool ok = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t native_error = 0;
    std::uint64_t diagnostic_flags = 0;
};

StringResult CollectOsVersion() noexcept;
DisplayResult CollectPrimaryDisplayMode() noexcept;
StringResult CollectCpuModel() noexcept;
StringResult CollectCpuid() noexcept;
UInt32Result CollectCpuCores() noexcept;
UInt64Result CollectMemoryGb() noexcept;
UInt64Result CollectStorageGb() noexcept;
StringResult CollectLocale() noexcept;
StringResult CollectHostname() noexcept;
StringResult CollectUsername() noexcept;

} // namespace internal
} // namespace DeviceInfoSDK
