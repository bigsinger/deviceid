#include "internal/SystemCollectors.h"

#include "DeviceInfoSDK/DeviceInfo.h"
#include "internal/Utf8.h"
#include "internal/WinHandle.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winnls.h>

#include <cstdio>
#include <cwchar>
#include <cstring>
#include <string>
#include <vector>

#if defined(_M_IX86) || defined(_M_X64)
#include <intrin.h>
#endif

namespace DeviceInfoSDK {
namespace internal {
namespace {

void SetError(StringResult* result, std::uint32_t error) noexcept {
    if (result != nullptr) {
        result->native_error = error;
        result->ok = false;
    }
}

std::uint64_t RoundUpGiB(unsigned long long bytes) noexcept {
    constexpr unsigned long long gib = 1024ull * 1024ull * 1024ull;
    if (bytes == 0) {
        return 0;
    }
    return (bytes + gib - 1) / gib;
}

} // namespace

StringResult CollectOsVersion() noexcept {
    StringResult result;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        SetError(&result, GetLastError());
        return result;
    }

    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (rtl_get_version == nullptr) {
        SetError(&result, GetLastError());
        return result;
    }

    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    const LONG status = rtl_get_version(&version);
    if (status < 0) {
        SetError(&result, static_cast<std::uint32_t>(status));
        return result;
    }

    char text[32]{};
    std::snprintf(
        text,
        sizeof(text),
        "%lu.%lu.%lu",
        static_cast<unsigned long>(version.dwMajorVersion),
        static_cast<unsigned long>(version.dwMinorVersion),
        static_cast<unsigned long>(version.dwBuildNumber));
    result.value = text;
    result.ok = true;
    return result;
}

DisplayResult CollectPrimaryDisplayMode() noexcept {
    DisplayResult result;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) != FALSE &&
        dm.dmPelsWidth > 0 &&
        dm.dmPelsHeight > 0) {
        result.ok = true;
        result.width = dm.dmPelsWidth;
        result.height = dm.dmPelsHeight;
        return result;
    }

    result.native_error = GetLastError();
    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);
    if (width > 0 && height > 0) {
        result.ok = true;
        result.width = static_cast<std::uint32_t>(width);
        result.height = static_cast<std::uint32_t>(height);
        result.diagnostic_flags = kDiagDisplayFallback;
        return result;
    }
    if (result.native_error == 0) {
        result.native_error = GetLastError();
    }
    return result;
}

StringResult CollectCpuModel() noexcept {
    StringResult result;
    UniqueRegKey key;
    LSTATUS status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        0,
        KEY_QUERY_VALUE,
        key.put());
    if (status != ERROR_SUCCESS) {
        SetError(&result, static_cast<std::uint32_t>(status));
        return result;
    }

    DWORD type = 0;
    DWORD bytes = 0;
    status = RegQueryValueExW(key.get(), L"ProcessorNameString", nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || type != REG_SZ || bytes == 0 || bytes > 512u * sizeof(wchar_t)) {
        SetError(&result, status == ERROR_SUCCESS ? ERROR_INVALID_DATA : static_cast<std::uint32_t>(status));
        return result;
    }

    std::vector<wchar_t> buffer((bytes + sizeof(wchar_t) - 1) / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(
        key.get(),
        L"ProcessorNameString",
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(buffer.data()),
        &bytes);
    if (status != ERROR_SUCCESS) {
        SetError(&result, static_cast<std::uint32_t>(status));
        return result;
    }

    std::size_t length = 0;
    while (length < buffer.size() && buffer[length] != L'\0') {
        ++length;
    }
    std::string utf8;
    std::uint32_t err = 0;
    if (!WideToUtf8(buffer.data(), static_cast<int>(length), &utf8, &err)) {
        SetError(&result, err);
        return result;
    }
    result.value = TrimAndCollapseAsciiWhitespace(utf8);
    result.ok = !result.value.empty();
    if (!result.ok) {
        result.native_error = ERROR_INVALID_DATA;
    }
    return result;
}

UInt32Result CollectCpuCores() noexcept {
    UInt32Result result;
    DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (count == 0) {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        count = info.dwNumberOfProcessors;
    }
    if (count == 0) {
        result.native_error = GetLastError();
        return result;
    }
    result.ok = true;
    result.value = count;
    return result;
}

StringResult CollectCpuid() noexcept {
    StringResult result;
#if defined(_M_IX86) || defined(_M_X64)
    int regs[4]{};
    __cpuid(regs, 0);
    const int max_leaf = regs[0];
    if (max_leaf < 1) {
        SetError(&result, ERROR_NOT_SUPPORTED);
        return result;
    }

    char vendor[13]{};
    std::memcpy(vendor + 0, &regs[1], sizeof(int));
    std::memcpy(vendor + 4, &regs[3], sizeof(int));
    std::memcpy(vendor + 8, &regs[2], sizeof(int));

    int leaf1[4]{};
    __cpuid(leaf1, 1);
    const unsigned int eax = static_cast<unsigned int>(leaf1[0]);
    const unsigned int stepping = eax & 0x0Fu;
    const unsigned int model = (eax >> 4) & 0x0Fu;
    const unsigned int family = (eax >> 8) & 0x0Fu;
    const unsigned int ext_model = (eax >> 16) & 0x0Fu;
    const unsigned int ext_family = (eax >> 20) & 0xFFu;
    const unsigned int display_family = family == 0x0Fu ? family + ext_family : family;
    const unsigned int display_model =
        (family == 0x06u || family == 0x0Fu) ? model + (ext_model << 4) : model;

    char text[128]{};
    std::snprintf(
        text,
        sizeof(text),
        "%s-family%02x-model%03x-stepping%x-eax%08x",
        vendor,
        display_family,
        display_model,
        stepping,
        eax);
    result.value = text;
    result.ok = true;
    return result;
#else
    SetError(&result, ERROR_NOT_SUPPORTED);
    return result;
#endif
}

UInt64Result CollectMemoryGb() noexcept {
    UInt64Result result;
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) == FALSE) {
        result.native_error = GetLastError();
        return result;
    }
    result.ok = true;
    result.value = RoundUpGiB(status.ullTotalPhys);
    return result;
}

UInt64Result CollectStorageGb() noexcept {
    UInt64Result result;
    DWORD needed = GetLogicalDriveStringsW(0, nullptr);
    if (needed == 0) {
        result.native_error = GetLastError();
        return result;
    }

    std::vector<wchar_t> drives(static_cast<std::size_t>(needed) + 1, L'\0');
    if (GetLogicalDriveStringsW(needed, drives.data()) == 0) {
        result.native_error = GetLastError();
        return result;
    }

    unsigned long long total_bytes = 0;
    bool found_fixed_drive = false;
    std::uint32_t first_error = 0;
    const wchar_t* cursor = drives.data();
    while (*cursor != L'\0') {
        const UINT drive_type = GetDriveTypeW(cursor);
        if (drive_type == DRIVE_FIXED) {
            ULARGE_INTEGER total{};
            if (GetDiskFreeSpaceExW(cursor, nullptr, &total, nullptr) != FALSE) {
                const unsigned long long previous = total_bytes;
                total_bytes += total.QuadPart;
                if (total_bytes < previous) {
                    result.native_error = ERROR_ARITHMETIC_OVERFLOW;
                    return result;
                }
                found_fixed_drive = true;
            } else if (first_error == 0) {
                first_error = GetLastError();
            }
        }
        cursor += wcslen(cursor) + 1;
    }

    if (!found_fixed_drive) {
        result.native_error = first_error == 0 ? ERROR_NOT_FOUND : first_error;
        return result;
    }

    result.ok = true;
    result.value = RoundUpGiB(total_bytes);
    return result;
}

StringResult CollectLocale() noexcept {
    StringResult result;
    const LANGID langid = GetUserDefaultUILanguage();
    wchar_t locale_name[LOCALE_NAME_MAX_LENGTH]{};
    const LCID lcid = MAKELCID(langid, SORT_DEFAULT);
    const int written = LCIDToLocaleName(lcid, locale_name, LOCALE_NAME_MAX_LENGTH, 0);
    if (written <= 0) {
        SetError(&result, GetLastError());
        return result;
    }
    std::uint32_t err = 0;
    if (!WideToUtf8(locale_name, written - 1, &result.value, &err)) {
        SetError(&result, err);
        return result;
    }
    result.ok = true;
    return result;
}

StringResult CollectHostname() noexcept {
    StringResult result;
    DWORD size = 0;
    GetComputerNameExW(ComputerNamePhysicalDnsHostname, nullptr, &size);
    DWORD err = GetLastError();
    if (size == 0 && err != ERROR_MORE_DATA) {
        SetError(&result, err);
        return result;
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(size) + 1, L'\0');
    if (GetComputerNameExW(ComputerNamePhysicalDnsHostname, buffer.data(), &size) == FALSE) {
        SetError(&result, GetLastError());
        return result;
    }
    std::uint32_t utf8_err = 0;
    if (!WideToUtf8(buffer.data(), static_cast<int>(size), &result.value, &utf8_err)) {
        SetError(&result, utf8_err);
        return result;
    }
    result.ok = true;
    return result;
}

StringResult CollectUsername() noexcept {
    StringResult result;
    DWORD size = 0;
    GetUserNameW(nullptr, &size);
    DWORD err = GetLastError();
    if (size == 0 && err != ERROR_INSUFFICIENT_BUFFER) {
        SetError(&result, err);
        return result;
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(size) + 1, L'\0');
    if (GetUserNameW(buffer.data(), &size) == FALSE) {
        SetError(&result, GetLastError());
        return result;
    }
    if (size > 0 && buffer[size - 1] == L'\0') {
        --size;
    }
    std::uint32_t utf8_err = 0;
    if (!WideToUtf8(buffer.data(), static_cast<int>(size), &result.value, &utf8_err)) {
        SetError(&result, utf8_err);
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace internal
} // namespace DeviceInfoSDK
