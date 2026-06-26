#include <DeviceInfoSDK/DeviceInfo.h>

#include <iostream>

int main() {
    DeviceInfoSDK::DeviceInfoOptions options;
    options.id_namespace = "com.company.product";
    options.app_version = "1.0.0";

    DeviceInfoSDK::DeviceInfoResult result = DeviceInfoSDK::GetDeviceInfo(options);
    std::cout << "result_code=" << static_cast<unsigned int>(result.code) << "\n";
    std::cout << "device_id=" << result.info.device_id << "\n";
    return result.code == DeviceInfoSDK::ResultCode::kOk ? 0 : 1;
}
