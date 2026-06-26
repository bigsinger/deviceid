#include "DeviceInfoSDK/DeviceInfoC.h"

#include "DeviceInfoSDK/DeviceInfo.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>

namespace {

constexpr std::uint32_t kOptionFlagLegacyMigration = 1u << 0;

bool HasTerminator(const char* value, std::size_t capacity) noexcept {
    return std::memchr(value, '\0', capacity) != nullptr;
}

std::string FixedString(const char* value, std::size_t capacity) {
    const char* end = static_cast<const char*>(std::memchr(value, '\0', capacity));
    if (end == nullptr) {
        return {};
    }
    return std::string(value, end);
}

void CopyString(char* target, std::size_t capacity, const std::string& value) noexcept {
    if (target == nullptr || capacity == 0) {
        return;
    }
    const std::size_t count = (std::min)(capacity - 1, value.size());
    if (count > 0) {
        std::memcpy(target, value.data(), count);
    }
    target[count] = '\0';
}

bool ConvertOptions(
    const DISDK_OptionsV2* source,
    bool require_app_version,
    DeviceInfoSDK::DeviceInfoOptions* options) noexcept {
    if (source == nullptr || options == nullptr ||
        source->struct_size < sizeof(DISDK_OptionsV2) ||
        source->abi_version != DISDK_ABI_VERSION ||
        !HasTerminator(source->id_namespace, sizeof(source->id_namespace)) ||
        !HasTerminator(source->app_version, sizeof(source->app_version)) ||
        !HasTerminator(source->app_channel, sizeof(source->app_channel))) {
        return false;
    }

    options->id_namespace = FixedString(source->id_namespace, sizeof(source->id_namespace));
    options->app_version = FixedString(source->app_version, sizeof(source->app_version));
    options->app_channel = FixedString(source->app_channel, sizeof(source->app_channel));
    options->collection_flags = source->collection_flags;
    options->lock_timeout_ms = source->lock_timeout_ms;
    options->enable_legacy_migration = (source->option_flags & kOptionFlagLegacyMigration) != 0;
    return !require_app_version || !options->app_version.empty();
}

void FillFullOutput(const DeviceInfoSDK::DeviceInfoResult& result, DISDK_DeviceInfoV2* output) noexcept {
    std::memset(output, 0, sizeof(*output));
    output->struct_size = sizeof(DISDK_DeviceInfoV2);
    output->abi_version = DISDK_ABI_VERSION;
    CopyString(output->platform, sizeof(output->platform), result.info.platform);
    CopyString(output->device_id, sizeof(output->device_id), result.info.device_id);
    CopyString(output->app_version, sizeof(output->app_version), result.info.app_version);
    CopyString(output->app_channel, sizeof(output->app_channel), result.info.app_channel);
    CopyString(output->os, sizeof(output->os), result.info.os);
    CopyString(output->os_version, sizeof(output->os_version), result.info.os_version);
    output->screen_width = result.info.screen_width;
    output->screen_height = result.info.screen_height;
    CopyString(output->model, sizeof(output->model), result.info.model);
    CopyString(output->brand, sizeof(output->brand), result.info.brand);
    CopyString(output->mac, sizeof(output->mac), result.info.mac);
    CopyString(output->cpuid, sizeof(output->cpuid), result.info.cpuid);
    CopyString(output->cpu_model, sizeof(output->cpu_model), result.info.cpu_model);
    output->cpu_cores = result.info.cpu_cores;
    output->memory_gb = result.info.memory_gb;
    output->storage_gb = result.info.storage_gb;
    CopyString(output->network_type, sizeof(output->network_type), result.info.network_type);
    CopyString(output->lang, sizeof(output->lang), result.info.lang);
    CopyString(output->hostname, sizeof(output->hostname), result.info.hostname);
    CopyString(output->os_username, sizeof(output->os_username), result.info.os_username);
    output->present_mask = result.present_mask;
    output->error_mask = result.error_mask;
    output->diagnostic_flags = result.diagnostic_flags;
    output->result_code = static_cast<std::uint32_t>(result.code);
    output->native_error = result.native_error;
}

int32_t CopyResultToCaller(
    const DeviceInfoSDK::DeviceInfoResult& result,
    DISDK_DeviceInfoV2* caller_output) noexcept {
    if (caller_output == nullptr ||
        caller_output->struct_size < offsetof(DISDK_DeviceInfoV2, platform) ||
        caller_output->abi_version != DISDK_ABI_VERSION) {
        return static_cast<int32_t>(DeviceInfoSDK::ResultCode::kInvalidArgument);
    }

    const std::size_t caller_size = caller_output->struct_size;
    DISDK_DeviceInfoV2 full{};
    FillFullOutput(result, &full);
    std::memset(caller_output, 0, caller_size);
    std::memcpy(caller_output, &full, (std::min)(caller_size, sizeof(full)));
    return static_cast<int32_t>(result.code);
}

DeviceInfoSDK::DeviceInfoResult InvalidResult() noexcept {
    DeviceInfoSDK::DeviceInfoResult result;
    result.code = DeviceInfoSDK::ResultCode::kInvalidArgument;
    result.native_error = 87;
    return result;
}

} // namespace

extern "C" {

DISDK_API int32_t DISDK_CALL DISDK_GetDeviceInfoV2(
    const DISDK_OptionsV2* options,
    DISDK_DeviceInfoV2* output) {
    DeviceInfoSDK::DeviceInfoOptions cpp_options;
    if (!ConvertOptions(options, true, &cpp_options)) {
        if (output != nullptr &&
            output->struct_size >= offsetof(DISDK_DeviceInfoV2, platform) &&
            output->abi_version == DISDK_ABI_VERSION) {
            return CopyResultToCaller(InvalidResult(), output);
        }
        return static_cast<int32_t>(DeviceInfoSDK::ResultCode::kInvalidArgument);
    }
    DeviceInfoSDK::DeviceInfoResult result = DeviceInfoSDK::GetDeviceInfo(cpp_options);
    return CopyResultToCaller(result, output);
}

DISDK_API int32_t DISDK_CALL DISDK_DeletePersistedDeviceIdV2(
    const DISDK_OptionsV2* options,
    uint32_t* native_error) {
    DeviceInfoSDK::DeviceInfoOptions cpp_options;
    if (!ConvertOptions(options, false, &cpp_options)) {
        if (native_error != nullptr) {
            *native_error = 87;
        }
        return static_cast<int32_t>(DeviceInfoSDK::ResultCode::kInvalidArgument);
    }
    std::uint32_t err = 0;
    DeviceInfoSDK::ResultCode code = DeviceInfoSDK::DeletePersistedDeviceId(cpp_options, &err);
    if (native_error != nullptr) {
        *native_error = err;
    }
    return static_cast<int32_t>(code);
}

DISDK_API const char* DISDK_CALL DISDK_GetSdkVersion(void) {
    return DeviceInfoSDK::GetSdkVersion();
}

} // extern "C"
