#include "internal/Utf8.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>

namespace DeviceInfoSDK {
namespace internal {

bool IsValidUtf8(const char* data, std::size_t length) noexcept {
    std::size_t i = 0;
    while (i < length) {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        if (c == 0) {
            return false;
        }
        if (c <= 0x7F) {
            ++i;
            continue;
        }

        std::uint32_t codepoint = 0;
        std::size_t needed = 0;
        if ((c & 0xE0) == 0xC0) {
            codepoint = c & 0x1F;
            needed = 1;
            if (codepoint == 0) {
                return false;
            }
        } else if ((c & 0xF0) == 0xE0) {
            codepoint = c & 0x0F;
            needed = 2;
        } else if ((c & 0xF8) == 0xF0) {
            codepoint = c & 0x07;
            needed = 3;
        } else {
            return false;
        }

        if (i + needed >= length) {
            return false;
        }
        for (std::size_t j = 1; j <= needed; ++j) {
            const unsigned char t = static_cast<unsigned char>(data[i + j]);
            if ((t & 0xC0) != 0x80) {
                return false;
            }
            codepoint = (codepoint << 6) | (t & 0x3F);
        }

        if ((needed == 1 && codepoint < 0x80) ||
            (needed == 2 && codepoint < 0x800) ||
            (needed == 3 && codepoint < 0x10000)) {
            return false;
        }
        if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return false;
        }
        i += needed + 1;
    }
    return true;
}

bool IsValidUtf8(const std::string& value) noexcept {
    return IsValidUtf8(value.data(), value.size());
}

bool HasEmbeddedNul(const std::string& value) noexcept {
    return value.find('\0') != std::string::npos;
}

std::string TrimAsciiWhitespace(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return value.substr(first, last - first);
}

std::string CollapseAsciiWhitespace(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    bool in_space = false;
    for (char ch : value) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (!in_space) {
                output.push_back(' ');
                in_space = true;
            }
        } else {
            output.push_back(ch);
            in_space = false;
        }
    }
    return output;
}

std::string TrimAndCollapseAsciiWhitespace(const std::string& value) {
    return CollapseAsciiWhitespace(TrimAsciiWhitespace(value));
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });
    return value;
}

bool CopyTruncatedUtf8(std::string* value, std::size_t max_bytes) noexcept {
    if (value == nullptr || value->size() <= max_bytes) {
        return false;
    }
    std::size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>((*value)[end]) & 0xC0) == 0x80) {
        --end;
    }
    value->resize(end);
    return true;
}

bool WideToUtf8(const wchar_t* value, int length, std::string* output, std::uint32_t* native_error) noexcept {
    if (output == nullptr || value == nullptr || length < 0) {
        if (native_error != nullptr) {
            *native_error = ERROR_INVALID_PARAMETER;
        }
        return false;
    }
    output->clear();
    if (length == 0) {
        return true;
    }
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        if (native_error != nullptr) {
            *native_error = GetLastError();
        }
        return false;
    }
    output->assign(static_cast<std::size_t>(needed), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value,
        length,
        &(*output)[0],
        needed,
        nullptr,
        nullptr);
    if (written != needed) {
        if (native_error != nullptr) {
            *native_error = GetLastError();
        }
        output->clear();
        return false;
    }
    return true;
}

bool WideToUtf8(const std::wstring& value, std::string* output, std::uint32_t* native_error) noexcept {
    if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        if (native_error != nullptr) {
            *native_error = ERROR_ARITHMETIC_OVERFLOW;
        }
        return false;
    }
    return WideToUtf8(value.data(), static_cast<int>(value.size()), output, native_error);
}

std::wstring AsciiToWide(const std::string& value) {
    std::wstring output;
    output.reserve(value.size());
    for (char ch : value) {
        output.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
    }
    return output;
}

} // namespace internal
} // namespace DeviceInfoSDK
