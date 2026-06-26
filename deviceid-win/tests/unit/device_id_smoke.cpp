#include <DeviceInfoSDK/DeviceInfo.h>

#include <cassert>

int main() {
    DeviceInfoSDK::DeviceInfoOptions options;
    options.id_namespace = "com.bigsinger.deviceid.test";
    options.app_version = "2.0.0";

    std::uint32_t native_error = 0;
    DeviceInfoSDK::DeletePersistedDeviceId(options, &native_error);

    DeviceInfoSDK::DeviceInfoResult first = DeviceInfoSDK::GetDeviceInfo(options);
    DeviceInfoSDK::DeviceInfoResult second = DeviceInfoSDK::GetDeviceInfo(options);

    assert(!first.info.device_id.empty());
    assert(first.info.device_id == second.info.device_id);
    assert(first.info.mac.empty());
    assert(first.info.hostname.empty());
    assert(first.info.os_username.empty());
    return 0;
}
