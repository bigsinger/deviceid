#include <DeviceInfoSDK/DeviceInfoC.h>

#include <stdio.h>
#include <string.h>

int main(void) {
    DISDK_OptionsV2 options;
    DISDK_DeviceInfoV2 output;
    memset(&options, 0, sizeof(options));
    memset(&output, 0, sizeof(output));

    options.struct_size = sizeof(options);
    options.abi_version = DISDK_ABI_VERSION;
    options.collection_flags = DISDK_COLLECT_DEFAULT;
    options.lock_timeout_ms = 2000;
    strcpy_s(options.id_namespace, sizeof(options.id_namespace), "com.company.product");
    strcpy_s(options.app_version, sizeof(options.app_version), "1.0.0");

    output.struct_size = sizeof(output);
    output.abi_version = DISDK_ABI_VERSION;

    int code = DISDK_GetDeviceInfoV2(&options, &output);
    printf("result_code=%d\n", code);
    printf("device_id=%s\n", output.device_id);
    return code == 0 ? 0 : 1;
}
