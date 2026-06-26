#include "DeviceInfoSDK/DeviceInfo.h"
#include "DeviceInfoSDK/DeviceInfoC.h"
#include "internal/DeviceIdManager.h"
#include "internal/Utf8.h"

#include <cstddef>
#include <cwchar>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct CliOptions {
    DeviceInfoSDK::DeviceInfoOptions sdk;
    std::string format = "text";
    bool self_test = false;
};

std::string WideArgToUtf8(const wchar_t* value) {
    std::string output;
    std::uint32_t err = 0;
    if (value == nullptr ||
        !DeviceInfoSDK::internal::WideToUtf8(value, static_cast<int>(wcslen(value)), &output, &err)) {
        return {};
    }
    return output;
}

void PrintUsage() {
    std::cerr
        << "deviceinfo_cli\n"
        << "deviceinfo_cli -e json\n";
}

bool ParseArgs(int argc, wchar_t* argv[], CliOptions* options) {
    if (options == nullptr) {
        return false;
    }
    options->sdk.collection_flags =
        DeviceInfoSDK::kCollectDefault |
        DeviceInfoSDK::kCollectMac |
        DeviceInfoSDK::kCollectCpuid |
        DeviceInfoSDK::kCollectHostname |
        DeviceInfoSDK::kCollectUsername;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = WideArgToUtf8(argv[i]);
        if (arg == "-e") {
            if (i + 1 >= argc) {
                return false;
            }
            options->format = WideArgToUtf8(argv[++i]);
            if (options->format != "json") {
                return false;
            }
        } else if (arg == "--self-test") {
            options->self_test = true;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            std::exit(0);
        } else {
            return false;
        }
    }
    return true;
}

std::string JsonEscape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char ch : value) {
        switch (ch) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (ch < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
            } else {
                out << static_cast<char>(ch);
            }
            break;
        }
    }
    return out.str();
}

std::string Hex64(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

void PrintJsonFields(const DeviceInfoSDK::DeviceInfoResult& result, std::ostream& out) {
    const auto& info = result.info;
    out << "{";
    out << "\"platform\":\"" << JsonEscape(info.platform) << "\",";
    out << "\"device_id\":\"" << JsonEscape(info.device_id) << "\",";
    out << "\"app_version\":\"" << JsonEscape(info.app_version) << "\",";
    out << "\"app_channel\":\"" << JsonEscape(info.app_channel) << "\",";
    out << "\"os\":\"" << JsonEscape(info.os) << "\",";
    out << "\"os_version\":\"" << JsonEscape(info.os_version) << "\",";
    out << "\"screen_width\":" << info.screen_width << ",";
    out << "\"screen_height\":" << info.screen_height << ",";
    out << "\"model\":\"" << JsonEscape(info.model) << "\",";
    out << "\"brand\":\"" << JsonEscape(info.brand) << "\",";
    out << "\"mac\":\"" << JsonEscape(info.mac) << "\",";
    out << "\"cpuid\":\"" << JsonEscape(info.cpuid) << "\",";
    out << "\"cpu_model\":\"" << JsonEscape(info.cpu_model) << "\",";
    out << "\"cpu_cores\":" << info.cpu_cores << ",";
    out << "\"memory_gb\":" << info.memory_gb << ",";
    out << "\"storage_gb\":" << info.storage_gb << ",";
    out << "\"network_type\":\"" << JsonEscape(info.network_type) << "\",";
    out << "\"lang\":\"" << JsonEscape(info.lang) << "\",";
    out << "\"hostname\":\"" << JsonEscape(info.hostname) << "\",";
    out << "\"os_username\":\"" << JsonEscape(info.os_username) << "\",";
    out << "\"_status\":{";
    out << "\"result_code\":" << static_cast<std::uint32_t>(result.code) << ",";
    out << "\"present_mask\":\"" << Hex64(result.present_mask) << "\",";
    out << "\"error_mask\":\"" << Hex64(result.error_mask) << "\",";
    out << "\"diagnostic_flags\":\"" << Hex64(result.diagnostic_flags) << "\",";
    out << "\"native_error\":" << result.native_error;
    out << "}}";
}

void PrintText(const DeviceInfoSDK::DeviceInfoResult& result) {
    const auto& info = result.info;
    std::cout
        << "platform=" << info.platform << "\n"
        << "device_id=" << info.device_id << "\n"
        << "app_version=" << info.app_version << "\n"
        << "app_channel=" << info.app_channel << "\n"
        << "os=" << info.os << "\n"
        << "os_version=" << info.os_version << "\n"
        << "screen_width=" << info.screen_width << "\n"
        << "screen_height=" << info.screen_height << "\n"
        << "model=" << info.model << "\n"
        << "brand=" << info.brand << "\n"
        << "mac=" << info.mac << "\n"
        << "cpuid=" << info.cpuid << "\n"
        << "cpu_model=" << info.cpu_model << "\n"
        << "cpu_cores=" << info.cpu_cores << "\n"
        << "memory_gb=" << info.memory_gb << "\n"
        << "storage_gb=" << info.storage_gb << "\n"
        << "network_type=" << info.network_type << "\n"
        << "lang=" << info.lang << "\n"
        << "hostname=" << info.hostname << "\n"
        << "os_username=" << info.os_username << "\n"
        << "result_code=" << static_cast<std::uint32_t>(result.code) << "\n"
        << "present_mask=" << Hex64(result.present_mask) << "\n"
        << "error_mask=" << Hex64(result.error_mask) << "\n"
        << "diagnostic_flags=" << Hex64(result.diagnostic_flags) << "\n"
        << "native_error=" << result.native_error << "\n";
}

int ExitCode(DeviceInfoSDK::ResultCode code) noexcept {
    switch (code) {
    case DeviceInfoSDK::ResultCode::kOk:
    case DeviceInfoSDK::ResultCode::kPartial:
        return 0;
    case DeviceInfoSDK::ResultCode::kInvalidArgument:
        return 2;
    case DeviceInfoSDK::ResultCode::kDeviceIdVolatile:
    case DeviceInfoSDK::ResultCode::kBusy:
        return 3;
    case DeviceInfoSDK::ResultCode::kInternalError:
    default:
        return 4;
    }
}

bool RunSelfTest() {
    bool ok = true;
    const auto check = [&](bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << "\n";
        ok = ok && condition;
    };

    check(sizeof(DISDK_OptionsV2) >= 217 && sizeof(DISDK_OptionsV2) % 8 == 0, "c_abi_options_size");
    check(offsetof(DISDK_DeviceInfoV2, present_mask) > offsetof(DISDK_DeviceInfoV2, os_username), "c_abi_status_tail");

    DeviceInfoSDK::DeviceInfoOptions invalid;
    invalid.id_namespace = "com.example.product";
    check(DeviceInfoSDK::GetDeviceInfo(invalid).code == DeviceInfoSDK::ResultCode::kInvalidArgument, "reject_placeholder_namespace");

    DeviceInfoSDK::DeviceInfoOptions defaulted;
    DeviceInfoSDK::DeviceInfoResult defaulted_result = DeviceInfoSDK::GetDeviceInfo(defaulted);
    DeviceInfoSDK::DeviceInfoResult defaulted_again = DeviceInfoSDK::GetDeviceInfo(defaulted);
    check(!defaulted_result.info.device_id.empty(), "default_namespace_and_app_version");
    check(defaulted_result.info.device_id == defaulted_again.info.device_id, "default_device_id_stable");
    check(defaulted_result.info.app_version == DeviceInfoSDK::GetSdkVersion(), "default_app_version");

    DeviceInfoSDK::DeviceInfoOptions options;
    options.id_namespace = "com.bigsinger.deviceid.selftest";
    options.app_version = "2.0.0";
    options.collection_flags = DeviceInfoSDK::kCollectDefault;
    std::uint32_t native_error = 0;
    DeviceInfoSDK::DeletePersistedDeviceId(options, &native_error);
    DeviceInfoSDK::DeviceInfoResult first = DeviceInfoSDK::GetDeviceInfo(options);
    DeviceInfoSDK::DeviceInfoResult second = DeviceInfoSDK::GetDeviceInfo(options);
    std::string normalized;
    check(DeviceInfoSDK::internal::IsValidUuidV4Text(first.info.device_id, &normalized), "uuid_v4_format");
    check(first.info.device_id == second.info.device_id, "device_id_stable");
    check(first.info.mac.empty() && first.info.cpuid.empty() && first.info.hostname.empty() && first.info.os_username.empty(), "sensitive_default_empty");
    check((first.present_mask & (DeviceInfoSDK::kFieldMac | DeviceInfoSDK::kFieldCpuid | DeviceInfoSDK::kFieldHostname | DeviceInfoSDK::kFieldOsUsername)) == 0, "sensitive_default_not_present");

    DeviceInfoSDK::DeviceInfoOptions sensitive = options;
    sensitive.collection_flags |= DeviceInfoSDK::kCollectCpuid | DeviceInfoSDK::kCollectHostname | DeviceInfoSDK::kCollectUsername;
    DeviceInfoSDK::DeviceInfoResult sensitive_result = DeviceInfoSDK::GetDeviceInfo(sensitive);
    check(!sensitive_result.info.cpuid.empty(), "cpuid_collects");
    check(!sensitive_result.info.hostname.empty(), "hostname_collects");
    check(!sensitive_result.info.os_username.empty(), "username_collects");

    DISDK_OptionsV2 c_options{};
    c_options.struct_size = sizeof(c_options);
    c_options.abi_version = DISDK_ABI_VERSION;
    c_options.collection_flags = DISDK_COLLECT_DEFAULT;
    c_options.lock_timeout_ms = 2000;
    DISDK_DeviceInfoV2 c_output{};
    c_output.struct_size = sizeof(c_output);
    c_output.abi_version = DISDK_ABI_VERSION;
    const int32_t c_code = DISDK_GetDeviceInfoV2(&c_options, &c_output);
    check(c_code == static_cast<int32_t>(c_output.result_code), "c_abi_result_code");
    check(std::strlen(c_output.device_id) == 36, "c_abi_device_id");

    return ok;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    CliOptions options;
    if (!ParseArgs(argc, argv, &options)) {
        PrintUsage();
        return 2;
    }

    if (options.self_test) {
        return RunSelfTest() ? 0 : 4;
    }

    DeviceInfoSDK::DeviceInfoResult result = DeviceInfoSDK::GetDeviceInfo(options.sdk);

    if (options.format == "json") {
        PrintJsonFields(result, std::cout);
        std::cout << "\n";
    } else {
        PrintText(result);
    }

    return ExitCode(result.code);
}
