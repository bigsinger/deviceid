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
    bool include_sensitive = false;
    bool no_network = false;
    bool delete_device_id = false;
    bool show_status = false;
    bool self_test = false;
    int repeat = 1;
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
        << "deviceinfo_cli --namespace <id> --app-version <ver> [options]\n"
        << "  --format text|json        Output format, default text\n"
        << "  -e json                   Alias for --format json\n"
        << "  --app-channel <channel>   Optional app channel\n"
        << "  --include-sensitive       Enable mac, hostname and os_username\n"
        << "  --no-network              Disable network_type and mac collection\n"
        << "  --repeat <N>              Collect N times\n"
        << "  --delete-device-id        Delete persisted ID and exit\n"
        << "  --show-status             Show masks in text mode\n"
        << "  --self-test               Run local smoke tests\n";
}

bool ParseInt(const std::string& text, int* value) {
    if (value == nullptr || text.empty()) {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed < 1 || parsed > 10000) {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

bool ParseArgs(int argc, wchar_t* argv[], CliOptions* options) {
    if (options == nullptr) {
        return false;
    }
    options->sdk.collection_flags = DeviceInfoSDK::kCollectDefault;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = WideArgToUtf8(argv[i]);
        const auto need_value = [&](std::string* out) -> bool {
            if (i + 1 >= argc || out == nullptr) {
                return false;
            }
            *out = WideArgToUtf8(argv[++i]);
            return true;
        };

        if (arg == "--namespace") {
            if (!need_value(&options->sdk.id_namespace)) {
                return false;
            }
        } else if (arg == "--app-version") {
            if (!need_value(&options->sdk.app_version)) {
                return false;
            }
        } else if (arg == "--app-channel") {
            if (!need_value(&options->sdk.app_channel)) {
                return false;
            }
        } else if (arg == "--format") {
            if (!need_value(&options->format)) {
                return false;
            }
        } else if (arg == "-e") {
            if (!need_value(&options->format)) {
                return false;
            }
        } else if (arg == "--include-sensitive") {
            options->include_sensitive = true;
        } else if (arg == "--no-network") {
            options->no_network = true;
        } else if (arg == "--repeat") {
            std::string repeat_text;
            if (!need_value(&repeat_text) || !ParseInt(repeat_text, &options->repeat)) {
                return false;
            }
        } else if (arg == "--delete-device-id") {
            options->delete_device_id = true;
        } else if (arg == "--show-status") {
            options->show_status = true;
        } else if (arg == "--self-test") {
            options->self_test = true;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            std::exit(0);
        } else {
            return false;
        }
    }

    if (options->include_sensitive) {
        options->sdk.collection_flags |=
            DeviceInfoSDK::kCollectMac |
            DeviceInfoSDK::kCollectHostname |
            DeviceInfoSDK::kCollectUsername;
    }
    if (options->no_network) {
        options->sdk.collection_flags &= ~DeviceInfoSDK::kCollectNetworkType;
        options->sdk.collection_flags &= ~DeviceInfoSDK::kCollectMac;
    }
    return options->format == "text" || options->format == "json";
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

void PrintText(const DeviceInfoSDK::DeviceInfoResult& result, bool show_status) {
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
        << "os_username=" << info.os_username << "\n";
    if (show_status) {
        std::cout
            << "result_code=" << static_cast<std::uint32_t>(result.code) << "\n"
            << "present_mask=" << Hex64(result.present_mask) << "\n"
            << "error_mask=" << Hex64(result.error_mask) << "\n"
            << "diagnostic_flags=" << Hex64(result.diagnostic_flags) << "\n"
            << "native_error=" << result.native_error << "\n";
    }
}

int ExitCode(DeviceInfoSDK::ResultCode code) noexcept {
    switch (code) {
    case DeviceInfoSDK::ResultCode::kOk:
        return 0;
    case DeviceInfoSDK::ResultCode::kPartial:
        return 1;
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

DeviceInfoSDK::ResultCode WorseCode(DeviceInfoSDK::ResultCode a, DeviceInfoSDK::ResultCode b) noexcept {
    const auto rank = [](DeviceInfoSDK::ResultCode code) {
        switch (code) {
        case DeviceInfoSDK::ResultCode::kInvalidArgument:
            return 5;
        case DeviceInfoSDK::ResultCode::kDeviceIdVolatile:
            return 4;
        case DeviceInfoSDK::ResultCode::kBusy:
            return 3;
        case DeviceInfoSDK::ResultCode::kInternalError:
            return 2;
        case DeviceInfoSDK::ResultCode::kPartial:
            return 1;
        case DeviceInfoSDK::ResultCode::kOk:
        default:
            return 0;
        }
    };
    return rank(a) >= rank(b) ? a : b;
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
    invalid.app_version = "1.0.0";
    check(DeviceInfoSDK::GetDeviceInfo(invalid).code == DeviceInfoSDK::ResultCode::kInvalidArgument, "reject_placeholder_namespace");

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
    check(first.info.mac.empty() && first.info.hostname.empty() && first.info.os_username.empty(), "sensitive_default_empty");
    check((first.present_mask & (DeviceInfoSDK::kFieldMac | DeviceInfoSDK::kFieldHostname | DeviceInfoSDK::kFieldOsUsername)) == 0, "sensitive_default_not_present");

    DISDK_OptionsV2 c_options{};
    c_options.struct_size = sizeof(c_options);
    c_options.abi_version = DISDK_ABI_VERSION;
    c_options.collection_flags = DISDK_COLLECT_DEFAULT;
    c_options.lock_timeout_ms = 2000;
    strcpy_s(c_options.id_namespace, "com.bigsinger.deviceid.selftest");
    strcpy_s(c_options.app_version, "2.0.0");
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

    if (options.sdk.id_namespace.empty()) {
        PrintUsage();
        return 2;
    }

    if (options.delete_device_id) {
        std::uint32_t native_error = 0;
        const DeviceInfoSDK::ResultCode code = DeviceInfoSDK::DeletePersistedDeviceId(options.sdk, &native_error);
        if (options.format == "json") {
            std::cout
                << "{\"result_code\":" << static_cast<std::uint32_t>(code)
                << ",\"native_error\":" << native_error << "}\n";
        } else {
            std::cout
                << "result_code=" << static_cast<std::uint32_t>(code) << "\n"
                << "native_error=" << native_error << "\n";
        }
        return ExitCode(code);
    }

    if (options.sdk.app_version.empty()) {
        PrintUsage();
        return 2;
    }

    std::vector<DeviceInfoSDK::DeviceInfoResult> results;
    results.reserve(static_cast<std::size_t>(options.repeat));
    DeviceInfoSDK::ResultCode aggregate = DeviceInfoSDK::ResultCode::kOk;
    for (int i = 0; i < options.repeat; ++i) {
        DeviceInfoSDK::DeviceInfoResult result = DeviceInfoSDK::GetDeviceInfo(options.sdk);
        aggregate = WorseCode(aggregate, result.code);
        results.push_back(result);
    }

    if (options.format == "json") {
        if (results.size() == 1) {
            PrintJsonFields(results.front(), std::cout);
            std::cout << "\n";
        } else {
            std::cout << "{\"runs\":[";
            for (std::size_t i = 0; i < results.size(); ++i) {
                if (i != 0) {
                    std::cout << ",";
                }
                PrintJsonFields(results[i], std::cout);
            }
            std::cout << "]}\n";
        }
    } else {
        for (std::size_t i = 0; i < results.size(); ++i) {
            if (results.size() > 1) {
                std::cout << "# run=" << (i + 1) << "\n";
            }
            PrintText(results[i], options.show_status);
        }
    }

    return ExitCode(aggregate);
}
