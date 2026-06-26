#include <DeviceInfoSDK/DeviceInfoC.h>

#include <cstddef>

static_assert(DISDK_ABI_VERSION == 2, "ABI version must remain 2 for V2");
static_assert(offsetof(DISDK_DeviceInfoV2, platform) == 8, "platform offset changed");
static_assert(offsetof(DISDK_DeviceInfoV2, present_mask) > offsetof(DISDK_DeviceInfoV2, os_username), "status fields must stay after data");
static_assert(sizeof(DISDK_OptionsV2) % 8 == 0, "options struct must keep pack(8) alignment");

int main() {
    return 0;
}
