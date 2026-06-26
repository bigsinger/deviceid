#include "internal/SmbiosCollector.h"

#include "internal/Utf8.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace DeviceInfoSDK {
namespace internal {
namespace {

#pragma pack(push, 1)
struct RawSmbiosDataHeader {
    BYTE used20_calling_method;
    BYTE smbios_major_version;
    BYTE smbios_minor_version;
    BYTE dmi_revision;
    DWORD length;
};

struct SmbiosStructureHeader {
    BYTE type;
    BYTE length;
    WORD handle;
};
#pragma pack(pop)

constexpr DWORD kRsmbProvider =
    static_cast<DWORD>('R') |
    (static_cast<DWORD>('S') << 8) |
    (static_cast<DWORD>('M') << 16) |
    (static_cast<DWORD>('B') << 24);

bool IsPlaceholder(const std::string& value) {
    const std::string lower = ToLowerAscii(value);
    return lower == "to be filled by o.e.m." ||
           lower == "default string" ||
           lower == "system product name" ||
           lower == "not specified" ||
           lower == "not applicable" ||
           lower == "unknown" ||
           lower == "none" ||
           lower == "n/a";
}

std::string CleanSmbiosString(const std::string& raw, std::size_t max_bytes) {
    if (!IsValidUtf8(raw)) {
        return {};
    }
    std::string value = TrimAndCollapseAsciiWhitespace(raw);
    if (value.empty() || value.size() > max_bytes || IsPlaceholder(value)) {
        return {};
    }
    return value;
}

bool FindStringAreaEnd(
    const unsigned char* table,
    std::size_t table_len,
    std::size_t string_start,
    std::size_t* next_offset) noexcept {
    if (string_start >= table_len) {
        return false;
    }
    for (std::size_t i = string_start; i + 1 < table_len; ++i) {
        if (table[i] == 0 && table[i + 1] == 0) {
            if (next_offset != nullptr) {
                *next_offset = i + 2;
            }
            return true;
        }
    }
    return false;
}

std::string GetIndexedString(
    const unsigned char* table,
    std::size_t string_start,
    std::size_t next_offset,
    unsigned int index) {
    if (index == 0) {
        return {};
    }
    std::size_t current = string_start;
    unsigned int current_index = 1;
    while (current < next_offset) {
        std::size_t end = current;
        while (end < next_offset && table[end] != 0) {
            ++end;
        }
        if (end == current) {
            break;
        }
        if (current_index == index) {
            return std::string(
                reinterpret_cast<const char*>(table + current),
                reinterpret_cast<const char*>(table + end));
        }
        ++current_index;
        current = end + 1;
    }
    return {};
}

} // namespace

SmbiosResult CollectSmbiosSystemInfo() noexcept {
    SmbiosResult result;
    const UINT required = GetSystemFirmwareTable(kRsmbProvider, 0, nullptr, 0);
    if (required == 0) {
        result.native_error = GetLastError();
        return result;
    }
    if (required <= sizeof(RawSmbiosDataHeader) || required > 1024u * 1024u) {
        result.native_error = ERROR_INVALID_DATA;
        return result;
    }

    std::vector<unsigned char> buffer(required);
    const UINT written = GetSystemFirmwareTable(kRsmbProvider, 0, buffer.data(), required);
    if (written == 0 || written != required) {
        result.native_error = GetLastError();
        if (result.native_error == 0) {
            result.native_error = ERROR_INVALID_DATA;
        }
        return result;
    }

    const auto* raw = reinterpret_cast<const RawSmbiosDataHeader*>(buffer.data());
    const std::size_t table_offset = sizeof(RawSmbiosDataHeader);
    if (raw->length == 0 || table_offset + raw->length > buffer.size()) {
        result.native_error = ERROR_INVALID_DATA;
        return result;
    }

    const unsigned char* table = buffer.data() + table_offset;
    const std::size_t table_len = raw->length;
    std::size_t offset = 0;
    while (offset + sizeof(SmbiosStructureHeader) <= table_len) {
        const auto* header = reinterpret_cast<const SmbiosStructureHeader*>(table + offset);
        if (header->length < sizeof(SmbiosStructureHeader) || offset + header->length > table_len) {
            result.native_error = ERROR_INVALID_DATA;
            return result;
        }

        const std::size_t string_start = offset + header->length;
        std::size_t next_offset = 0;
        if (!FindStringAreaEnd(table, table_len, string_start, &next_offset)) {
            result.native_error = ERROR_INVALID_DATA;
            return result;
        }

        if (header->type == 1) {
            if (header->length < 0x06) {
                result.native_error = ERROR_INVALID_DATA;
                return result;
            }
            const unsigned int manufacturer_index = table[offset + 0x04];
            const unsigned int product_index = table[offset + 0x05];
            result.brand = CleanSmbiosString(
                GetIndexedString(table, string_start, next_offset, manufacturer_index),
                127);
            result.model = CleanSmbiosString(
                GetIndexedString(table, string_start, next_offset, product_index),
                255);
            result.ok = true;
            return result;
        }

        offset = next_offset;
    }

    result.ok = true;
    return result;
}

} // namespace internal
} // namespace DeviceInfoSDK
