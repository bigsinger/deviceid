#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace DeviceInfoSDK {
namespace internal {

bool IsValidUtf8(const char* data, std::size_t length) noexcept;
bool IsValidUtf8(const std::string& value) noexcept;
bool HasEmbeddedNul(const std::string& value) noexcept;
std::string TrimAsciiWhitespace(const std::string& value);
std::string CollapseAsciiWhitespace(const std::string& value);
std::string TrimAndCollapseAsciiWhitespace(const std::string& value);
std::string ToLowerAscii(std::string value);
bool CopyTruncatedUtf8(std::string* value, std::size_t max_bytes) noexcept;
bool WideToUtf8(const wchar_t* value, int length, std::string* output, std::uint32_t* native_error) noexcept;
bool WideToUtf8(const std::wstring& value, std::string* output, std::uint32_t* native_error) noexcept;
std::wstring AsciiToWide(const std::string& value);

} // namespace internal
} // namespace DeviceInfoSDK
