#include "internal/DeviceIdManager.h"

#include "internal/Utf8.h"
#include "internal/WinHandle.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <knownfolders.h>
#include <sddl.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifndef HKEY_CURRENT_USER_LOCAL_SETTINGS
#define HKEY_CURRENT_USER_LOCAL_SETTINGS ((HKEY)(ULONG_PTR)((LONG)0x80000007))
#endif

namespace DeviceInfoSDK {
namespace internal {
namespace {

constexpr wchar_t kRegistryPathPrefix[] = L"Software\\DeviceInfoSDK\\Namespaces\\";
constexpr wchar_t kRegistryValueName[] = L"DeviceId";
constexpr wchar_t kLocalDirName[] = L"DeviceInfoSDK";
constexpr wchar_t kDeviceIdFileName[] = L"device_id";
constexpr wchar_t kDeviceIdLockFileName[] = L"device_id.lock";
constexpr DWORD kMaxLockTimeoutMs = 30000;

std::atomic<unsigned long> g_temp_counter{0};
std::atomic<unsigned long> g_fallback_counter{0};

class LocalFreeString {
public:
    LocalFreeString() noexcept = default;
    explicit LocalFreeString(PWSTR value) noexcept : value_(value) {}
    ~LocalFreeString() noexcept {
        if (value_ != nullptr) {
            LocalFree(value_);
        }
    }
    LocalFreeString(const LocalFreeString&) = delete;
    LocalFreeString& operator=(const LocalFreeString&) = delete;
    PWSTR get() const noexcept { return value_; }
    PWSTR* put() noexcept {
        if (value_ != nullptr) {
            LocalFree(value_);
        }
        value_ = nullptr;
        return &value_;
    }

private:
    PWSTR value_ = nullptr;
};

class SecurityDescriptorHandle {
public:
    SecurityDescriptorHandle() noexcept = default;
    ~SecurityDescriptorHandle() noexcept {
        if (value_ != nullptr) {
            LocalFree(value_);
        }
    }
    SecurityDescriptorHandle(const SecurityDescriptorHandle&) = delete;
    SecurityDescriptorHandle& operator=(const SecurityDescriptorHandle&) = delete;
    PSECURITY_DESCRIPTOR get() const noexcept { return value_; }
    PSECURITY_DESCRIPTOR* put() noexcept {
        if (value_ != nullptr) {
            LocalFree(value_);
        }
        value_ = nullptr;
        return &value_;
    }

private:
    PSECURITY_DESCRIPTOR value_ = nullptr;
};

struct StoreRead {
    bool valid = false;
    bool missing = true;
    std::string value;
    DWORD native_error = 0;
};

struct WriteResult {
    bool ok = false;
    DWORD native_error = 0;
};

struct StorePaths {
    std::wstring sdk_dir;
    std::wstring namespace_dir;
    std::wstring id_file;
    std::wstring lock_file;
};

struct AcquiredLock {
    bool acquired = false;
    bool abandoned = false;
    bool timed_out = false;
    DWORD native_error = 0;
    UniqueHandle named_mutex;
    UniqueHandle file_lock;
};

std::timed_mutex& MutexForNamespace(const std::string& id_namespace) {
    static std::mutex map_mutex;
    static std::map<std::string, std::unique_ptr<std::timed_mutex>> locks;

    std::lock_guard<std::mutex> guard(map_mutex);
    auto it = locks.find(id_namespace);
    if (it == locks.end()) {
        auto inserted = locks.emplace(id_namespace, std::unique_ptr<std::timed_mutex>(new std::timed_mutex()));
        it = inserted.first;
    }
    return *it->second;
}

bool IsHex(char ch) noexcept {
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

bool IsUuidVariant(char ch) noexcept {
    ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    return ch == '8' || ch == '9' || ch == 'a' || ch == 'b';
}

std::wstring RegistryPathForNamespace(const std::string& id_namespace) {
    return std::wstring(kRegistryPathPrefix) + AsciiToWide(id_namespace);
}

bool EnsureDirectory(const std::wstring& path, DWORD* native_error) noexcept {
    if (CreateDirectoryW(path.c_str(), nullptr) != FALSE) {
        return true;
    }
    const DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
        return true;
    }
    if (native_error != nullptr && *native_error == 0) {
        *native_error = err;
    }
    return false;
}

bool BuildStorePaths(const std::string& id_namespace, StorePaths* paths, DWORD* native_error) noexcept {
    if (paths == nullptr) {
        if (native_error != nullptr) {
            *native_error = ERROR_INVALID_PARAMETER;
        }
        return false;
    }

    CoTaskMemString local_app_data;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, local_app_data.put());
    if (FAILED(hr) || !local_app_data) {
        if (native_error != nullptr) {
            *native_error = static_cast<DWORD>(hr);
        }
        return false;
    }

    std::wstring base(local_app_data.get());
    paths->sdk_dir = base + L"\\" + kLocalDirName;
    paths->namespace_dir = paths->sdk_dir + L"\\" + AsciiToWide(id_namespace);
    paths->id_file = paths->namespace_dir + L"\\" + kDeviceIdFileName;
    paths->lock_file = paths->namespace_dir + L"\\" + kDeviceIdLockFileName;
    return true;
}

bool EnsureNamespaceDirectory(const StorePaths& paths, DWORD* native_error) noexcept {
    bool ok = EnsureDirectory(paths.sdk_dir, native_error);
    ok = EnsureDirectory(paths.namespace_dir, native_error) && ok;
    return ok;
}

StoreRead ReadRegistryValue(const std::string& id_namespace) noexcept {
    StoreRead result;
    UniqueRegKey key;
    const std::wstring key_path = RegistryPathForNamespace(id_namespace);
    LSTATUS status = RegOpenKeyExW(
        HKEY_CURRENT_USER_LOCAL_SETTINGS,
        key_path.c_str(),
        0,
        KEY_QUERY_VALUE,
        key.put());
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
        return result;
    }
    if (status != ERROR_SUCCESS) {
        result.missing = false;
        result.native_error = static_cast<DWORD>(status);
        return result;
    }

    DWORD type = 0;
    DWORD bytes = 0;
    status = RegQueryValueExW(key.get(), kRegistryValueName, nullptr, &type, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) {
        return result;
    }
    if (status != ERROR_SUCCESS) {
        result.missing = false;
        result.native_error = static_cast<DWORD>(status);
        return result;
    }
    if ((type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0 || bytes > 128u * sizeof(wchar_t)) {
        result.missing = false;
        result.native_error = ERROR_INVALID_DATA;
        return result;
    }

    std::vector<wchar_t> buffer((bytes + sizeof(wchar_t) - 1) / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(
        key.get(),
        kRegistryValueName,
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(buffer.data()),
        &bytes);
    if (status != ERROR_SUCCESS) {
        result.missing = false;
        result.native_error = static_cast<DWORD>(status);
        return result;
    }

    std::size_t length = 0;
    while (length < buffer.size() && buffer[length] != L'\0') {
        ++length;
    }
    std::string utf8;
    std::uint32_t err = 0;
    if (!WideToUtf8(buffer.data(), static_cast<int>(length), &utf8, &err)) {
        result.missing = false;
        result.native_error = err;
        return result;
    }

    std::string normalized;
    if (!IsValidUuidV4Text(utf8, &normalized)) {
        result.missing = false;
        result.native_error = ERROR_INVALID_DATA;
        return result;
    }
    result.valid = true;
    result.missing = false;
    result.value = normalized;
    return result;
}

StoreRead ReadFileValue(const std::string& id_namespace) noexcept {
    StoreRead result;
    StorePaths paths;
    DWORD err = 0;
    if (!BuildStorePaths(id_namespace, &paths, &err)) {
        result.missing = false;
        result.native_error = err;
        return result;
    }

    UniqueHandle file(CreateFileW(
        paths.id_file.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (!file) {
        err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return result;
        }
        result.missing = false;
        result.native_error = err;
        return result;
    }

    LARGE_INTEGER size{};
    if (GetFileSizeEx(file.get(), &size) == FALSE || size.QuadPart < 0 || size.QuadPart > 128) {
        result.missing = false;
        result.native_error = GetLastError() == ERROR_SUCCESS ? ERROR_INVALID_DATA : GetLastError();
        return result;
    }

    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!bytes.empty()) {
        if (ReadFile(file.get(), &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr) == FALSE ||
            read != bytes.size()) {
            result.missing = false;
            result.native_error = GetLastError();
            return result;
        }
    }

    std::string normalized;
    if (!IsValidUuidV4Text(bytes, &normalized)) {
        result.missing = false;
        result.native_error = ERROR_INVALID_DATA;
        return result;
    }
    result.valid = true;
    result.missing = false;
    result.value = normalized;
    return result;
}

WriteResult WriteRegistryValue(const std::string& id_namespace, const std::string& uuid) noexcept {
    WriteResult result;
    UniqueRegKey key;
    const std::wstring key_path = RegistryPathForNamespace(id_namespace);
    LSTATUS status = RegCreateKeyExW(
        HKEY_CURRENT_USER_LOCAL_SETTINGS,
        key_path.c_str(),
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        key.put(),
        nullptr);
    if (status != ERROR_SUCCESS) {
        result.native_error = static_cast<DWORD>(status);
        return result;
    }
    const std::wstring wide = AsciiToWide(uuid);
    status = RegSetValueExW(
        key.get(),
        kRegistryValueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(wide.c_str()),
        static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) {
        result.native_error = static_cast<DWORD>(status);
        return result;
    }
    result.ok = true;
    return result;
}

WriteResult WriteFileValue(const std::string& id_namespace, const std::string& uuid) noexcept {
    WriteResult result;
    StorePaths paths;
    DWORD err = 0;
    if (!BuildStorePaths(id_namespace, &paths, &err) || !EnsureNamespaceDirectory(paths, &err)) {
        result.native_error = err == 0 ? ERROR_PATH_NOT_FOUND : err;
        return result;
    }

    const unsigned long counter = ++g_temp_counter;
    wchar_t temp_suffix[96]{};
    swprintf_s(
        temp_suffix,
        L"device_id.tmp.%lu.%lu",
        static_cast<unsigned long>(GetCurrentProcessId()),
        counter);
    const std::wstring temp_path = paths.namespace_dir + L"\\" + temp_suffix;

    UniqueHandle file(CreateFileW(
        temp_path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
        nullptr));
    if (!file) {
        result.native_error = GetLastError();
        return result;
    }

    const std::string content = uuid + "\n";
    DWORD written = 0;
    if (WriteFile(file.get(), content.data(), static_cast<DWORD>(content.size()), &written, nullptr) == FALSE ||
        written != content.size()) {
        result.native_error = GetLastError();
        file.reset();
        DeleteFileW(temp_path.c_str());
        return result;
    }
    if (FlushFileBuffers(file.get()) == FALSE) {
        result.native_error = GetLastError();
        file.reset();
        DeleteFileW(temp_path.c_str());
        return result;
    }
    file.reset();

    if (MoveFileExW(
            temp_path.c_str(),
            paths.id_file.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        result.native_error = GetLastError();
        DeleteFileW(temp_path.c_str());
        return result;
    }

    result.ok = true;
    return result;
}

bool GenerateUuidV4(std::string* uuid, std::uint32_t* native_error) noexcept {
    if (uuid == nullptr) {
        if (native_error != nullptr) {
            *native_error = ERROR_INVALID_PARAMETER;
        }
        return false;
    }
    unsigned char bytes[16]{};
    NTSTATUS status = BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        if (native_error != nullptr) {
            *native_error = static_cast<DWORD>(status);
        }
        return false;
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

    char output[37]{};
    std::snprintf(
        output,
        sizeof(output),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0],
        bytes[1],
        bytes[2],
        bytes[3],
        bytes[4],
        bytes[5],
        bytes[6],
        bytes[7],
        bytes[8],
        bytes[9],
        bytes[10],
        bytes[11],
        bytes[12],
        bytes[13],
        bytes[14],
        bytes[15]);
    uuid->assign(output);
    return true;
}

std::string GenerateFallbackId() noexcept {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = ft.dwLowDateTime;
    ticks.HighPart = ft.dwHighDateTime;
    const unsigned long long unix_ms = (ticks.QuadPart - 116444736000000000ull) / 10000ull;

    char output[128]{};
    std::snprintf(
        output,
        sizeof(output),
        "fallback-%llu-%lu-%lu-%lu",
        unix_ms,
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()),
        ++g_fallback_counter);
    return output;
}

bool GetCurrentUserSidString(std::wstring* sid, DWORD* native_error) noexcept {
    if (sid == nullptr) {
        if (native_error != nullptr) {
            *native_error = ERROR_INVALID_PARAMETER;
        }
        return false;
    }
    UniqueHandle token;
    HANDLE raw_token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token) == FALSE) {
        if (native_error != nullptr) {
            *native_error = GetLastError();
        }
        return false;
    }
    token.reset(raw_token);

    DWORD needed = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &needed);
    if (needed == 0) {
        if (native_error != nullptr) {
            *native_error = GetLastError();
        }
        return false;
    }
    std::vector<unsigned char> buffer(needed);
    if (GetTokenInformation(token.get(), TokenUser, buffer.data(), needed, &needed) == FALSE) {
        if (native_error != nullptr) {
            *native_error = GetLastError();
        }
        return false;
    }
    TOKEN_USER* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
    LocalFreeString sid_text;
    if (ConvertSidToStringSidW(user->User.Sid, sid_text.put()) == FALSE) {
        if (native_error != nullptr) {
            *native_error = GetLastError();
        }
        return false;
    }
    sid->assign(sid_text.get());
    return true;
}

bool Sha256Hex128(const std::string& input, std::wstring* output, DWORD* native_error) noexcept {
    if (output == nullptr) {
        if (native_error != nullptr) {
            *native_error = ERROR_INVALID_PARAMETER;
        }
        return false;
    }

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_length = 0;
    DWORD data_length = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) {
        if (native_error != nullptr) {
            *native_error = static_cast<DWORD>(status);
        }
        return false;
    }

    status = BCryptGetProperty(
        alg,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_length),
        sizeof(object_length),
        &data_length,
        0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        if (native_error != nullptr) {
            *native_error = static_cast<DWORD>(status);
        }
        return false;
    }

    std::vector<unsigned char> object(object_length);
    std::vector<unsigned char> digest(32);
    status = BCryptCreateHash(alg, &hash, object.data(), object_length, nullptr, 0, 0);
    if (status >= 0) {
        status = BCryptHashData(
            hash,
            reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
            static_cast<ULONG>(input.size()),
            0);
    }
    if (status >= 0) {
        status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    }

    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);

    if (status < 0) {
        if (native_error != nullptr) {
            *native_error = static_cast<DWORD>(status);
        }
        return false;
    }

    wchar_t text[33]{};
    for (int i = 0; i < 16; ++i) {
        swprintf_s(text + i * 2, 3, L"%02x", static_cast<unsigned int>(digest[static_cast<std::size_t>(i)]));
    }
    output->assign(text, 32);
    return true;
}

bool BuildMutexName(const std::string& id_namespace, std::wstring* mutex_name, DWORD* native_error) noexcept {
    std::wstring sid;
    if (!GetCurrentUserSidString(&sid, native_error)) {
        return false;
    }
    std::string sid_ascii;
    sid_ascii.reserve(sid.size());
    for (wchar_t ch : sid) {
        sid_ascii.push_back(static_cast<char>(ch));
    }

    std::wstring hex;
    if (!Sha256Hex128(sid_ascii + "|" + id_namespace, &hex, native_error)) {
        return false;
    }
    mutex_name->assign(L"Global\\DeviceInfoSDK-" + hex);
    return true;
}

bool BuildMutexSecurityAttributes(const std::wstring& sid, SECURITY_ATTRIBUTES* attributes, SecurityDescriptorHandle* sd) noexcept {
    if (attributes == nullptr || sd == nullptr) {
        return false;
    }
    const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + sid + L")";
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(),
            SDDL_REVISION_1,
            sd->put(),
            nullptr) == FALSE) {
        return false;
    }
    attributes->nLength = sizeof(SECURITY_ATTRIBUTES);
    attributes->lpSecurityDescriptor = sd->get();
    attributes->bInheritHandle = FALSE;
    return true;
}

AcquiredLock TryAcquireNamedMutex(const std::string& id_namespace, DWORD timeout_ms) noexcept {
    AcquiredLock result;
    std::wstring mutex_name;
    DWORD err = 0;
    if (!BuildMutexName(id_namespace, &mutex_name, &err)) {
        result.native_error = err;
        return result;
    }

    std::wstring sid;
    SecurityDescriptorHandle sd;
    SECURITY_ATTRIBUTES attributes{};
    SECURITY_ATTRIBUTES* attributes_ptr = nullptr;
    if (GetCurrentUserSidString(&sid, nullptr) && BuildMutexSecurityAttributes(sid, &attributes, &sd)) {
        attributes_ptr = &attributes;
    }

    UniqueHandle mutex(CreateMutexW(attributes_ptr, FALSE, mutex_name.c_str()));
    if (!mutex) {
        err = GetLastError();
        HANDLE opened = OpenMutexW(SYNCHRONIZE, FALSE, mutex_name.c_str());
        if (opened == nullptr) {
            result.native_error = err;
            return result;
        }
        mutex.reset(opened);
    }

    const DWORD wait = WaitForSingleObject(mutex.get(), timeout_ms);
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
        result.acquired = true;
        result.abandoned = wait == WAIT_ABANDONED;
        result.named_mutex = std::move(mutex);
        return result;
    }
    if (wait == WAIT_TIMEOUT) {
        result.timed_out = true;
        result.native_error = WAIT_TIMEOUT;
        return result;
    }
    result.native_error = GetLastError();
    return result;
}

AcquiredLock TryAcquireFileLock(const std::string& id_namespace, DWORD timeout_ms) noexcept {
    AcquiredLock result;
    StorePaths paths;
    DWORD err = 0;
    if (!BuildStorePaths(id_namespace, &paths, &err) || !EnsureNamespaceDirectory(paths, &err)) {
        result.native_error = err == 0 ? ERROR_PATH_NOT_FOUND : err;
        return result;
    }

    const DWORD start = GetTickCount();
    for (;;) {
        UniqueHandle lock(CreateFileW(
            paths.lock_file.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
            nullptr));
        if (lock) {
            result.acquired = true;
            result.file_lock = std::move(lock);
            return result;
        }

        err = GetLastError();
        if (err != ERROR_SHARING_VIOLATION && err != ERROR_LOCK_VIOLATION) {
            result.native_error = err;
            return result;
        }
        if (timeout_ms == 0 || GetTickCount() - start >= timeout_ms) {
            result.timed_out = true;
            result.native_error = WAIT_TIMEOUT;
            return result;
        }
        Sleep(25);
    }
}

AcquiredLock AcquireNamespaceLock(const std::string& id_namespace, DWORD timeout_ms) noexcept {
    AcquiredLock lock = TryAcquireNamedMutex(id_namespace, timeout_ms);
    if (lock.acquired || lock.timed_out) {
        return lock;
    }
    return TryAcquireFileLock(id_namespace, timeout_ms);
}

void ReleaseNamespaceLock(AcquiredLock* lock) noexcept {
    if (lock == nullptr || !lock->acquired) {
        return;
    }
    if (lock->named_mutex) {
        ReleaseMutex(lock->named_mutex.get());
    }
    lock->named_mutex.reset();
    lock->file_lock.reset();
    lock->acquired = false;
}

DeviceIdOutcome BuildBusyOutcome(const std::string& id_namespace, DWORD native_error) noexcept {
    DeviceIdOutcome outcome;
    StoreRead reg = ReadRegistryValue(id_namespace);
    StoreRead file = ReadFileValue(id_namespace);
    if (reg.valid) {
        outcome.id = reg.value;
        outcome.code = ResultCode::kBusy;
        outcome.native_error = native_error;
        return outcome;
    }
    if (file.valid) {
        outcome.id = file.value;
        outcome.code = ResultCode::kBusy;
        outcome.native_error = native_error;
        return outcome;
    }
    outcome.id = GenerateFallbackId();
    outcome.code = ResultCode::kDeviceIdVolatile;
    outcome.diagnostic_flags = kDiagFallbackId;
    outcome.native_error = native_error;
    return outcome;
}

bool ReadLegacyDeviceId(std::string* normalized) noexcept {
    if (normalized == nullptr) {
        return false;
    }

    UniqueRegKey key;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\MyCompany\\MyApp", 0, KEY_QUERY_VALUE, key.put());
    if (status == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD bytes = 0;
        status = RegQueryValueExW(key.get(), L"DeviceId", nullptr, &type, nullptr, &bytes);
        if (status == ERROR_SUCCESS && type == REG_SZ && bytes > 0 && bytes <= 128u * sizeof(wchar_t)) {
            std::vector<wchar_t> buffer((bytes + sizeof(wchar_t) - 1) / sizeof(wchar_t) + 1, L'\0');
            if (RegQueryValueExW(
                    key.get(),
                    L"DeviceId",
                    nullptr,
                    &type,
                    reinterpret_cast<LPBYTE>(buffer.data()),
                    &bytes) == ERROR_SUCCESS) {
                std::size_t length = 0;
                while (length < buffer.size() && buffer[length] != L'\0') {
                    ++length;
                }
                std::string utf8;
                if (WideToUtf8(buffer.data(), static_cast<int>(length), &utf8, nullptr) &&
                    IsValidUuidV4Text(utf8, normalized)) {
                    return true;
                }
            }
        }
    }

    wchar_t profile[MAX_PATH]{};
    const DWORD profile_len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (profile_len == 0 || profile_len >= MAX_PATH) {
        return false;
    }
    const std::wstring path = std::wstring(profile) + L"\\.deviceid";
    UniqueHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        return false;
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file.get(), &size) == FALSE || size.QuadPart < 0 || size.QuadPart > 128) {
        return false;
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!bytes.empty() &&
        (ReadFile(file.get(), &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr) == FALSE ||
         read != bytes.size())) {
        return false;
    }
    return IsValidUuidV4Text(bytes, normalized);
}

void RememberFirstError(DWORD candidate, std::uint32_t* native_error) noexcept {
    if (native_error != nullptr && *native_error == 0 && candidate != 0) {
        *native_error = candidate;
    }
}

DeviceIdOutcome ResolveStoresLocked(const DeviceInfoOptions& options) noexcept {
    DeviceIdOutcome outcome;
    std::uint32_t first_error = 0;
    StoreRead reg = ReadRegistryValue(options.id_namespace);
    StoreRead file = ReadFileValue(options.id_namespace);
    RememberFirstError(reg.native_error, &first_error);
    RememberFirstError(file.native_error, &first_error);

    if (reg.valid && file.valid && reg.value == file.value) {
        outcome.id = reg.value;
        outcome.code = ResultCode::kOk;
        outcome.native_error = first_error;
        return outcome;
    }

    if (reg.valid) {
        outcome.id = reg.value;
        outcome.code = ResultCode::kPartial;
        if (file.valid && file.value != reg.value) {
            outcome.diagnostic_flags |= kDiagStoreConflict;
        }
        WriteResult written = WriteFileValue(options.id_namespace, reg.value);
        if (written.ok) {
            outcome.diagnostic_flags |= kDiagFileRepaired;
        } else {
            RememberFirstError(written.native_error, &first_error);
        }
        outcome.native_error = first_error;
        return outcome;
    }

    if (file.valid) {
        outcome.id = file.value;
        outcome.code = ResultCode::kPartial;
        WriteResult written = WriteRegistryValue(options.id_namespace, file.value);
        if (written.ok) {
            outcome.diagnostic_flags |= kDiagRegistryRepaired;
        } else {
            RememberFirstError(written.native_error, &first_error);
        }
        outcome.native_error = first_error;
        return outcome;
    }

    std::string candidate;
    if (options.enable_legacy_migration && ReadLegacyDeviceId(&candidate)) {
        outcome.diagnostic_flags |= kDiagLegacyMigrated;
    } else if (GenerateUuidV4(&candidate, &first_error)) {
        outcome.diagnostic_flags |= kDiagDeviceIdGenerated;
    } else {
        outcome.id = GenerateFallbackId();
        outcome.code = ResultCode::kDeviceIdVolatile;
        outcome.diagnostic_flags |= kDiagFallbackId;
        outcome.native_error = first_error;
        return outcome;
    }

    WriteResult reg_write = WriteRegistryValue(options.id_namespace, candidate);
    WriteResult file_write = WriteFileValue(options.id_namespace, candidate);
    RememberFirstError(reg_write.native_error, &first_error);
    RememberFirstError(file_write.native_error, &first_error);

    if (reg_write.ok && file_write.ok) {
        outcome.id = candidate;
        outcome.code = ResultCode::kOk;
        outcome.native_error = first_error;
        return outcome;
    }

    if (reg_write.ok || file_write.ok) {
        outcome.id = candidate;
        outcome.code = ResultCode::kPartial;
        outcome.native_error = first_error;
        return outcome;
    }

    outcome.id = GenerateFallbackId();
    outcome.code = ResultCode::kDeviceIdVolatile;
    outcome.diagnostic_flags |= kDiagFallbackId;
    outcome.native_error = first_error;
    return outcome;
}

} // namespace

bool IsValidNamespace(const std::string& value) noexcept {
    if (value.size() < 3 || value.size() > 64 || value == "com.example.product") {
        return false;
    }
    const auto valid_char = [](char ch) noexcept {
        return (ch >= 'a' && ch <= 'z') ||
               (ch >= '0' && ch <= '9') ||
               ch == '.' ||
               ch == '_' ||
               ch == '-';
    };
    if (!((value[0] >= 'a' && value[0] <= 'z') || (value[0] >= '0' && value[0] <= '9'))) {
        return false;
    }
    for (char ch : value) {
        if (!valid_char(ch)) {
            return false;
        }
    }
    if (value.find("..") != std::string::npos || value.find(".\\") != std::string::npos ||
        value.find("./") != std::string::npos) {
        return false;
    }
    return true;
}

bool IsValidUuidV4Text(const std::string& text, std::string* normalized) noexcept {
    if (HasEmbeddedNul(text)) {
        return false;
    }
    std::string value = TrimAsciiWhitespace(text);
    if (value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB &&
        static_cast<unsigned char>(value[2]) == 0xBF) {
        return false;
    }
    if (value.size() != 36) {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        const bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
        if (dash) {
            if (value[i] != '-') {
                return false;
            }
        } else if (!IsHex(value[i])) {
            return false;
        }
    }
    if (value[14] != '4' || !IsUuidVariant(value[19])) {
        return false;
    }
    value = ToLowerAscii(value);
    if (normalized != nullptr) {
        *normalized = value;
    }
    return true;
}

DeviceIdOutcome GetOrCreateDeviceId(const DeviceInfoOptions& options) noexcept {
    try {
        DWORD timeout_ms = options.lock_timeout_ms;
        if (timeout_ms > kMaxLockTimeoutMs) {
            timeout_ms = kMaxLockTimeoutMs;
        }

        std::timed_mutex& in_process = MutexForNamespace(options.id_namespace);
        std::unique_lock<std::timed_mutex> local_lock(in_process, std::defer_lock);
        if (!local_lock.try_lock_for(std::chrono::milliseconds(timeout_ms))) {
            return BuildBusyOutcome(options.id_namespace, WAIT_TIMEOUT);
        }

        AcquiredLock cross_process = AcquireNamespaceLock(options.id_namespace, timeout_ms);
        if (!cross_process.acquired) {
            return BuildBusyOutcome(
                options.id_namespace,
                cross_process.native_error == 0 ? WAIT_TIMEOUT : cross_process.native_error);
        }
        DeviceIdOutcome outcome = ResolveStoresLocked(options);
        if (cross_process.abandoned && outcome.code == ResultCode::kOk) {
            outcome.code = ResultCode::kPartial;
        }
        if (cross_process.native_error != 0 && outcome.native_error == 0) {
            outcome.native_error = cross_process.native_error;
        }
        ReleaseNamespaceLock(&cross_process);
        return outcome;
    } catch (...) {
        DeviceIdOutcome outcome;
        outcome.id = GenerateFallbackId();
        outcome.code = ResultCode::kDeviceIdVolatile;
        outcome.diagnostic_flags = kDiagFallbackId;
        outcome.native_error = ERROR_UNHANDLED_EXCEPTION;
        return outcome;
    }
}

ResultCode DeletePersistedDeviceIdInternal(
    const DeviceInfoOptions& options,
    std::uint32_t* native_error) noexcept {
    try {
        if (native_error != nullptr) {
            *native_error = 0;
        }
        DWORD timeout_ms = options.lock_timeout_ms;
        if (timeout_ms > kMaxLockTimeoutMs) {
            timeout_ms = kMaxLockTimeoutMs;
        }

        std::timed_mutex& in_process = MutexForNamespace(options.id_namespace);
        std::unique_lock<std::timed_mutex> local_lock(in_process, std::defer_lock);
        if (!local_lock.try_lock_for(std::chrono::milliseconds(timeout_ms))) {
            if (native_error != nullptr) {
                *native_error = WAIT_TIMEOUT;
            }
            return ResultCode::kBusy;
        }

        AcquiredLock cross_process = AcquireNamespaceLock(options.id_namespace, timeout_ms);
        if (!cross_process.acquired) {
            if (native_error != nullptr) {
                *native_error = cross_process.native_error == 0 ? WAIT_TIMEOUT : cross_process.native_error;
            }
            return cross_process.timed_out ? ResultCode::kBusy : ResultCode::kPartial;
        }

        ResultCode code = ResultCode::kOk;
        const std::wstring key_path = RegistryPathForNamespace(options.id_namespace);
        UniqueRegKey key;
        LSTATUS status = RegOpenKeyExW(
            HKEY_CURRENT_USER_LOCAL_SETTINGS,
            key_path.c_str(),
            0,
            KEY_SET_VALUE,
            key.put());
        if (status == ERROR_SUCCESS) {
            status = RegDeleteValueW(key.get(), kRegistryValueName);
            if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
                if (native_error != nullptr && *native_error == 0) {
                    *native_error = static_cast<std::uint32_t>(status);
                }
                code = ResultCode::kPartial;
            }
        } else if (status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND) {
            if (native_error != nullptr && *native_error == 0) {
                *native_error = static_cast<std::uint32_t>(status);
            }
            code = ResultCode::kPartial;
        }

        StorePaths paths;
        DWORD err = 0;
        if (BuildStorePaths(options.id_namespace, &paths, &err)) {
            if (DeleteFileW(paths.id_file.c_str()) == FALSE) {
                err = GetLastError();
                if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
                    if (native_error != nullptr && *native_error == 0) {
                        *native_error = err;
                    }
                    code = ResultCode::kPartial;
                }
            }
            RemoveDirectoryW(paths.namespace_dir.c_str());
        } else {
            if (native_error != nullptr && *native_error == 0) {
                *native_error = err;
            }
            code = ResultCode::kPartial;
        }

        ReleaseNamespaceLock(&cross_process);
        return code;
    } catch (...) {
        if (native_error != nullptr) {
            *native_error = ERROR_UNHANDLED_EXCEPTION;
        }
        return ResultCode::kInternalError;
    }
}

} // namespace internal
} // namespace DeviceInfoSDK
