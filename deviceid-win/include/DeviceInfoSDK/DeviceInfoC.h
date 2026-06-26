#pragma once

#include <stdint.h>

#if defined(_WIN32) && defined(DISDK_BUILD_DLL)
#define DISDK_API __declspec(dllexport)
#elif defined(_WIN32) && defined(DISDK_USE_DLL)
#define DISDK_API __declspec(dllimport)
#else
#define DISDK_API
#endif

#if defined(_MSC_VER)
#define DISDK_CALL __cdecl
#else
#define DISDK_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DISDK_ABI_VERSION 2u

#define DISDK_COLLECT_HARDWARE (1ull << 0)
#define DISDK_COLLECT_DISPLAY (1ull << 1)
#define DISDK_COLLECT_NETWORK_TYPE (1ull << 2)
#define DISDK_COLLECT_LOCALE (1ull << 3)
#define DISDK_COLLECT_MAC (1ull << 16)
#define DISDK_COLLECT_HOSTNAME (1ull << 17)
#define DISDK_COLLECT_USERNAME (1ull << 18)
#define DISDK_COLLECT_CPUID (1ull << 19)
#define DISDK_COLLECT_DEFAULT \
    (DISDK_COLLECT_HARDWARE | DISDK_COLLECT_DISPLAY | DISDK_COLLECT_NETWORK_TYPE | DISDK_COLLECT_LOCALE)

#pragma pack(push, 8)

typedef struct DISDK_OptionsV2 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t collection_flags;
    uint32_t lock_timeout_ms;
    uint32_t option_flags;
    char id_namespace[65];
    char app_version[64];
    char app_channel[64];
} DISDK_OptionsV2;

typedef struct DISDK_DeviceInfoV2 {
    uint32_t struct_size;
    uint32_t abi_version;

    char platform[16];
    char device_id[64];
    char app_version[64];
    char app_channel[64];
    char os[32];
    char os_version[32];
    uint32_t screen_width;
    uint32_t screen_height;
    char model[256];
    char brand[128];
    char mac[32];
    char cpuid[128];
    char cpu_model[256];
    uint32_t cpu_cores;
    uint32_t reserved0;
    uint64_t memory_gb;
    uint64_t storage_gb;
    char network_type[16];
    char lang[32];
    char hostname[256];
    char os_username[256];

    uint64_t present_mask;
    uint64_t error_mask;
    uint64_t diagnostic_flags;
    uint32_t result_code;
    uint32_t native_error;
} DISDK_DeviceInfoV2;

#pragma pack(pop)

DISDK_API int32_t DISDK_CALL DISDK_GetDeviceInfoV2(
    const DISDK_OptionsV2* options,
    DISDK_DeviceInfoV2* output);

DISDK_API int32_t DISDK_CALL DISDK_DeletePersistedDeviceIdV2(
    const DISDK_OptionsV2* options,
    uint32_t* native_error);

DISDK_API const char* DISDK_CALL DISDK_GetSdkVersion(void);

#ifdef __cplusplus
}
#endif
