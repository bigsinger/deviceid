#include "DeviceInfoSDK/DeviceInfo.h"

#include "internal/DeviceIdManager.h"
#include "internal/NetworkCollector.h"
#include "internal/SmbiosCollector.h"
#include "internal/SystemCollectors.h"
#include "internal/Utf8.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <string>

namespace DeviceInfoSDK {
namespace {

constexpr std::uint32_t kMaxLockTimeoutMs = 30000;
constexpr const char* kDefaultNamespace = "com.bigsinger.deviceid";

void RememberFirstError(std::uint32_t candidate, DeviceInfoResult* result) noexcept {
    if (result != nullptr && result->native_error == 0 && candidate != 0) {
        result->native_error = candidate;
    }
}

void MarkError(std::uint64_t field, std::uint32_t native_error, DeviceInfoResult* result) noexcept {
    if (result == nullptr) {
        return;
    }
    result->error_mask |= field;
    RememberFirstError(native_error, result);
}

void AssignStringField(
    std::string* target,
    std::string value,
    std::size_t max_bytes,
    std::uint64_t field,
    DeviceInfoResult* result) noexcept {
    if (target == nullptr || result == nullptr) {
        return;
    }
    if (internal::CopyTruncatedUtf8(&value, max_bytes)) {
        result->diagnostic_flags |= kDiagOutputTruncated;
        result->error_mask |= field;
    }
    *target = value;
    if (!target->empty()) {
        result->present_mask |= field;
    }
}

DeviceInfoOptions NormalizeOptions(DeviceInfoOptions options) {
    if (options.id_namespace.empty()) {
        options.id_namespace = kDefaultNamespace;
    }
    if (options.app_version.empty()) {
        options.app_version = GetSdkVersion();
    }
    return options;
}

bool ValidateOptionsForGet(const DeviceInfoOptions& options) noexcept {
    if (!internal::IsValidNamespace(options.id_namespace)) {
        return false;
    }
    if (options.app_version.size() > 63 ||
        internal::HasEmbeddedNul(options.app_version) ||
        !internal::IsValidUtf8(options.app_version)) {
        return false;
    }
    if (options.app_channel.size() > 63 ||
        internal::HasEmbeddedNul(options.app_channel) ||
        !internal::IsValidUtf8(options.app_channel)) {
        return false;
    }
    if (options.lock_timeout_ms > kMaxLockTimeoutMs) {
        return false;
    }
    return true;
}

bool ValidateOptionsForDelete(const DeviceInfoOptions& options) noexcept {
    return internal::IsValidNamespace(options.id_namespace) && options.lock_timeout_ms <= kMaxLockTimeoutMs;
}

void ApplyStringResult(
    const internal::StringResult& source,
    std::string* target,
    std::size_t max_bytes,
    std::uint64_t field,
    DeviceInfoResult* result,
    bool empty_is_success = false) noexcept {
    if (source.ok) {
        AssignStringField(target, source.value, max_bytes, field, result);
        if (empty_is_success) {
            result->present_mask |= field;
        }
        result->diagnostic_flags |= source.diagnostic_flags;
    } else {
        MarkError(field, source.native_error, result);
    }
}

void ApplyUInt32Result(
    const internal::UInt32Result& source,
    std::uint32_t* target,
    std::uint64_t field,
    DeviceInfoResult* result) noexcept {
    if (source.ok) {
        *target = source.value;
        result->present_mask |= field;
    } else {
        MarkError(field, source.native_error, result);
    }
}

void ApplyUInt64Result(
    const internal::UInt64Result& source,
    std::uint64_t* target,
    std::uint64_t field,
    DeviceInfoResult* result) noexcept {
    if (source.ok) {
        *target = source.value;
        result->present_mask |= field;
    } else {
        MarkError(field, source.native_error, result);
    }
}

ResultCode AggregateCode(const DeviceInfoResult& result, ResultCode device_id_code) noexcept {
    if (device_id_code == ResultCode::kDeviceIdVolatile) {
        return ResultCode::kDeviceIdVolatile;
    }
    if (device_id_code == ResultCode::kBusy) {
        return ResultCode::kBusy;
    }
    if (device_id_code == ResultCode::kInternalError) {
        return ResultCode::kInternalError;
    }
    if (device_id_code == ResultCode::kPartial || result.error_mask != 0 ||
        (result.diagnostic_flags & (kDiagRegistryRepaired | kDiagFileRepaired | kDiagStoreConflict | kDiagOutputTruncated)) != 0) {
        return ResultCode::kPartial;
    }
    return ResultCode::kOk;
}

} // namespace

DeviceInfoResult GetDeviceInfo(const DeviceInfoOptions& options) noexcept {
    DeviceInfoResult result;
    try {
        DeviceInfoOptions normalized_options = NormalizeOptions(options);
        if (!ValidateOptionsForGet(normalized_options)) {
            result.code = ResultCode::kInvalidArgument;
            result.native_error = 87;
            return result;
        }

        result.info.platform = "windows";
        result.info.app_version = normalized_options.app_version;
        result.info.app_channel = normalized_options.app_channel;
        result.info.os = "Windows";
        result.info.cpuid.clear();
        result.present_mask |= kFieldPlatform | kFieldAppVersion | kFieldOs;
        if (!result.info.app_channel.empty()) {
            result.present_mask |= kFieldAppChannel;
        }

        internal::DeviceIdOutcome id = internal::GetOrCreateDeviceId(normalized_options);
        result.info.device_id = id.id;
        if (!id.id.empty()) {
            result.present_mask |= kFieldDeviceId;
        } else {
            result.error_mask |= kFieldDeviceId;
        }
        result.diagnostic_flags |= id.diagnostic_flags;
        RememberFirstError(id.native_error, &result);

        ApplyStringResult(
            internal::CollectOsVersion(),
            &result.info.os_version,
            31,
            kFieldOsVersion,
            &result);

        if ((normalized_options.collection_flags & kCollectDisplay) != 0) {
            internal::DisplayResult display = internal::CollectPrimaryDisplayMode();
            result.diagnostic_flags |= display.diagnostic_flags;
            if (display.ok) {
                result.info.screen_width = display.width;
                result.info.screen_height = display.height;
                result.present_mask |= kFieldScreenWidth | kFieldScreenHeight;
                if ((display.diagnostic_flags & kDiagDisplayFallback) != 0) {
                    result.error_mask |= kFieldScreenWidth | kFieldScreenHeight;
                    RememberFirstError(display.native_error, &result);
                }
            } else {
                MarkError(kFieldScreenWidth, display.native_error, &result);
                MarkError(kFieldScreenHeight, display.native_error, &result);
            }
        }

        if ((normalized_options.collection_flags & kCollectHardware) != 0) {
            internal::SmbiosResult smbios = internal::CollectSmbiosSystemInfo();
            if (smbios.ok) {
                AssignStringField(&result.info.brand, smbios.brand, 127, kFieldBrand, &result);
                AssignStringField(&result.info.model, smbios.model, 255, kFieldModel, &result);
            } else {
                MarkError(kFieldBrand, smbios.native_error, &result);
                MarkError(kFieldModel, smbios.native_error, &result);
            }

            ApplyStringResult(
                internal::CollectCpuModel(),
                &result.info.cpu_model,
                255,
                kFieldCpuModel,
                &result);
            ApplyUInt32Result(
                internal::CollectCpuCores(),
                &result.info.cpu_cores,
                kFieldCpuCores,
                &result);
            ApplyUInt64Result(
                internal::CollectMemoryGb(),
                &result.info.memory_gb,
                kFieldMemoryGb,
                &result);
            ApplyUInt64Result(
                internal::CollectStorageGb(),
                &result.info.storage_gb,
                kFieldStorageGb,
                &result);
        }

        if ((normalized_options.collection_flags & kCollectCpuid) != 0) {
            ApplyStringResult(
                internal::CollectCpuid(),
                &result.info.cpuid,
                127,
                kFieldCpuid,
                &result);
        }

        const bool collect_network_type = (normalized_options.collection_flags & kCollectNetworkType) != 0;
        const bool collect_mac = (normalized_options.collection_flags & kCollectMac) != 0;
        if (collect_network_type || collect_mac) {
            internal::NetworkResult network = internal::CollectNetwork(collect_mac);
            result.diagnostic_flags |= network.diagnostic_flags;
            if (collect_network_type) {
                result.info.network_type = network.ok ? network.network_type : "unknown";
                if (network.ok) {
                    result.present_mask |= kFieldNetworkType;
                } else {
                    MarkError(kFieldNetworkType, network.native_error, &result);
                }
            }
            if (collect_mac) {
                if (network.ok && !network.mac.empty()) {
                    result.info.mac = network.mac;
                    result.present_mask |= kFieldMac;
                } else {
                    MarkError(kFieldMac, network.native_error == 0 ? 1168 : network.native_error, &result);
                }
            }
        }

        if ((normalized_options.collection_flags & kCollectLocale) != 0) {
            ApplyStringResult(
                internal::CollectLocale(),
                &result.info.lang,
                31,
                kFieldLang,
                &result);
        }

        if ((normalized_options.collection_flags & kCollectHostname) != 0) {
            ApplyStringResult(
                internal::CollectHostname(),
                &result.info.hostname,
                255,
                kFieldHostname,
                &result);
        }

        if ((normalized_options.collection_flags & kCollectUsername) != 0) {
            ApplyStringResult(
                internal::CollectUsername(),
                &result.info.os_username,
                255,
                kFieldOsUsername,
                &result);
        }

        result.code = AggregateCode(result, id.code);
        return result;
    } catch (const std::exception&) {
        result.code = ResultCode::kInternalError;
        if (result.native_error == 0) {
            result.native_error = 574;
        }
        return result;
    } catch (...) {
        result.code = ResultCode::kInternalError;
        if (result.native_error == 0) {
            result.native_error = 574;
        }
        return result;
    }
}

ResultCode DeletePersistedDeviceId(
    const DeviceInfoOptions& options,
    std::uint32_t* native_error) noexcept {
    try {
        if (native_error != nullptr) {
            *native_error = 0;
        }
        DeviceInfoOptions normalized_options = NormalizeOptions(options);
        if (!ValidateOptionsForDelete(normalized_options)) {
            if (native_error != nullptr) {
                *native_error = 87;
            }
            return ResultCode::kInvalidArgument;
        }
        return internal::DeletePersistedDeviceIdInternal(normalized_options, native_error);
    } catch (...) {
        if (native_error != nullptr) {
            *native_error = 574;
        }
        return ResultCode::kInternalError;
    }
}

const char* GetSdkVersion() noexcept {
    return "2.0.0";
}

} // namespace DeviceInfoSDK
