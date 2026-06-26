#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

#include <utility>

namespace DeviceInfoSDK {
namespace internal {

class UniqueHandle {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~UniqueHandle() noexcept { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    HANDLE get() const noexcept { return handle_; }
    explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE release() noexcept {
        HANDLE value = handle_;
        handle_ = nullptr;
        return value;
    }

    void reset(HANDLE handle = nullptr) noexcept {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

class UniqueRegKey {
public:
    UniqueRegKey() noexcept = default;
    explicit UniqueRegKey(HKEY key) noexcept : key_(key) {}
    ~UniqueRegKey() noexcept { reset(); }

    UniqueRegKey(const UniqueRegKey&) = delete;
    UniqueRegKey& operator=(const UniqueRegKey&) = delete;

    UniqueRegKey(UniqueRegKey&& other) noexcept : key_(other.release()) {}
    UniqueRegKey& operator=(UniqueRegKey&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    HKEY get() const noexcept { return key_; }
    HKEY* put() noexcept {
        reset();
        return &key_;
    }
    explicit operator bool() const noexcept { return key_ != nullptr; }

    HKEY release() noexcept {
        HKEY value = key_;
        key_ = nullptr;
        return value;
    }

    void reset(HKEY key = nullptr) noexcept {
        if (key_ != nullptr) {
            RegCloseKey(key_);
        }
        key_ = key;
    }

private:
    HKEY key_ = nullptr;
};

class CoTaskMemString {
public:
    CoTaskMemString() noexcept = default;
    explicit CoTaskMemString(PWSTR value) noexcept : value_(value) {}
    ~CoTaskMemString() noexcept { reset(); }

    CoTaskMemString(const CoTaskMemString&) = delete;
    CoTaskMemString& operator=(const CoTaskMemString&) = delete;

    CoTaskMemString(CoTaskMemString&& other) noexcept : value_(other.release()) {}
    CoTaskMemString& operator=(CoTaskMemString&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    PWSTR get() const noexcept { return value_; }
    PWSTR* put() noexcept {
        reset();
        return &value_;
    }
    explicit operator bool() const noexcept { return value_ != nullptr; }

    PWSTR release() noexcept {
        PWSTR value = value_;
        value_ = nullptr;
        return value;
    }

    void reset(PWSTR value = nullptr) noexcept {
        if (value_ != nullptr) {
            CoTaskMemFree(value_);
        }
        value_ = value;
    }

private:
    PWSTR value_ = nullptr;
};

} // namespace internal
} // namespace DeviceInfoSDK
