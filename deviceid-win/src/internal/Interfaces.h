#pragma once

#include <cstdint>
#include <string>

namespace DeviceInfoSDK {
namespace internal {

struct IRegistryStore {
    virtual ~IRegistryStore() = default;
    virtual bool Read(std::string* value, std::uint32_t* native_error) noexcept = 0;
    virtual bool Write(const std::string& value, std::uint32_t* native_error) noexcept = 0;
    virtual bool Delete(std::uint32_t* native_error) noexcept = 0;
};

struct IFileStore {
    virtual ~IFileStore() = default;
    virtual bool Read(std::string* value, std::uint32_t* native_error) noexcept = 0;
    virtual bool WriteAtomic(const std::string& value, std::uint32_t* native_error) noexcept = 0;
    virtual bool Delete(std::uint32_t* native_error) noexcept = 0;
};

struct IRandomSource {
    virtual ~IRandomSource() = default;
    virtual bool Fill(std::uint8_t* bytes, std::uint32_t length, std::uint32_t* native_error) noexcept = 0;
};

struct ISystemQueries {
    virtual ~ISystemQueries() = default;
};

struct IClock {
    virtual ~IClock() = default;
    virtual std::uint64_t UnixMilliseconds() noexcept = 0;
};

} // namespace internal
} // namespace DeviceInfoSDK
