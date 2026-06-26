#pragma once

#include <cstdint>
#include <string>

namespace DeviceInfoSDK {

constexpr std::uint32_t kAbiVersion = 2;

enum CollectionFlags : std::uint64_t {
    kCollectHardware = 1ull << 0,
    kCollectDisplay = 1ull << 1,
    kCollectNetworkType = 1ull << 2,
    kCollectLocale = 1ull << 3,
    kCollectMac = 1ull << 16,
    kCollectHostname = 1ull << 17,
    kCollectUsername = 1ull << 18
};

constexpr std::uint64_t kCollectDefault =
    kCollectHardware | kCollectDisplay | kCollectNetworkType | kCollectLocale;

enum FieldMask : std::uint64_t {
    kFieldPlatform = 1ull << 0,
    kFieldDeviceId = 1ull << 1,
    kFieldAppVersion = 1ull << 2,
    kFieldAppChannel = 1ull << 3,
    kFieldOs = 1ull << 4,
    kFieldOsVersion = 1ull << 5,
    kFieldScreenWidth = 1ull << 6,
    kFieldScreenHeight = 1ull << 7,
    kFieldModel = 1ull << 8,
    kFieldBrand = 1ull << 9,
    kFieldMac = 1ull << 10,
    kFieldCpuid = 1ull << 11,
    kFieldCpuModel = 1ull << 12,
    kFieldCpuCores = 1ull << 13,
    kFieldMemoryGb = 1ull << 14,
    kFieldStorageGb = 1ull << 15,
    kFieldNetworkType = 1ull << 16,
    kFieldLang = 1ull << 17,
    kFieldHostname = 1ull << 18,
    kFieldOsUsername = 1ull << 19
};

enum class ResultCode : std::uint32_t {
    kOk = 0,
    kPartial = 1,
    kDeviceIdVolatile = 2,
    kInvalidArgument = 3,
    kBusy = 4,
    kInternalError = 5
};

enum DiagnosticFlags : std::uint64_t {
    kDiagNone = 0,
    kDiagDeviceIdGenerated = 1ull << 0,
    kDiagRegistryRepaired = 1ull << 1,
    kDiagFileRepaired = 1ull << 2,
    kDiagStoreConflict = 1ull << 3,
    kDiagLegacyMigrated = 1ull << 4,
    kDiagFallbackId = 1ull << 5,
    kDiagOutputTruncated = 1ull << 6,
    kDiagDisplayFallback = 1ull << 7,
    kDiagVirtualAdapterPossible = 1ull << 8
};

struct DeviceInfoOptions {
    std::string id_namespace;
    std::string app_version;
    std::string app_channel;
    std::uint64_t collection_flags = kCollectDefault;
    std::uint32_t lock_timeout_ms = 2000;
    bool enable_legacy_migration = false;
};

struct DeviceInfo {
    std::string platform;
    std::string device_id;
    std::string app_version;
    std::string app_channel;
    std::string os;
    std::string os_version;
    std::uint32_t screen_width = 0;
    std::uint32_t screen_height = 0;
    std::string model;
    std::string brand;
    std::string mac;
    std::string cpuid;
    std::string cpu_model;
    std::uint32_t cpu_cores = 0;
    std::uint64_t memory_gb = 0;
    std::uint64_t storage_gb = 0;
    std::string network_type;
    std::string lang;
    std::string hostname;
    std::string os_username;
};

struct DeviceInfoResult {
    DeviceInfo info;
    ResultCode code = ResultCode::kInternalError;
    std::uint64_t present_mask = 0;
    std::uint64_t error_mask = 0;
    std::uint64_t diagnostic_flags = 0;
    std::uint32_t native_error = 0;
};

DeviceInfoResult GetDeviceInfo(const DeviceInfoOptions& options) noexcept;

ResultCode DeletePersistedDeviceId(
    const DeviceInfoOptions& options,
    std::uint32_t* native_error = nullptr) noexcept;

const char* GetSdkVersion() noexcept;

} // namespace DeviceInfoSDK
