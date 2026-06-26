using System;
using System.Runtime.InteropServices;

internal static class DeviceInfoExample
{
    private const uint DISDK_ABI_VERSION = 2;
    private const ulong DISDK_COLLECT_DEFAULT = (1UL << 0) | (1UL << 1) | (1UL << 2) | (1UL << 3);

    [StructLayout(LayoutKind.Sequential, Pack = 8, CharSet = CharSet.Ansi)]
    private unsafe struct DISDK_OptionsV2
    {
        public uint struct_size;
        public uint abi_version;
        public ulong collection_flags;
        public uint lock_timeout_ms;
        public uint option_flags;
        public fixed byte id_namespace[65];
        public fixed byte app_version[64];
        public fixed byte app_channel[64];
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8, CharSet = CharSet.Ansi)]
    private unsafe struct DISDK_DeviceInfoV2
    {
        public uint struct_size;
        public uint abi_version;
        public fixed byte platform[16];
        public fixed byte device_id[64];
        public fixed byte app_version[64];
        public fixed byte app_channel[64];
        public fixed byte os[32];
        public fixed byte os_version[32];
        public uint screen_width;
        public uint screen_height;
        public fixed byte model[256];
        public fixed byte brand[128];
        public fixed byte mac[32];
        public fixed byte cpuid[128];
        public fixed byte cpu_model[256];
        public uint cpu_cores;
        public uint reserved0;
        public ulong memory_gb;
        public ulong storage_gb;
        public fixed byte network_type[16];
        public fixed byte lang[32];
        public fixed byte hostname[256];
        public fixed byte os_username[256];
        public ulong present_mask;
        public ulong error_mask;
        public ulong diagnostic_flags;
        public uint result_code;
        public uint native_error;
    }

    [DllImport("DeviceInfoSDK.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern unsafe int DISDK_GetDeviceInfoV2(DISDK_OptionsV2* options, DISDK_DeviceInfoV2* output);

    private static unsafe void CopyAscii(byte* target, int capacity, string value)
    {
        var bytes = System.Text.Encoding.UTF8.GetBytes(value);
        int count = Math.Min(capacity - 1, bytes.Length);
        for (int i = 0; i < count; i++) target[i] = bytes[i];
        target[count] = 0;
    }

    private static unsafe string ReadAscii(byte* value, int capacity)
    {
        int count = 0;
        while (count < capacity && value[count] != 0) count++;
        return System.Text.Encoding.UTF8.GetString(value, count);
    }

    private static unsafe int Main()
    {
        DISDK_OptionsV2 options = default;
        DISDK_DeviceInfoV2 output = default;
        options.struct_size = (uint)sizeof(DISDK_OptionsV2);
        options.abi_version = DISDK_ABI_VERSION;
        options.collection_flags = DISDK_COLLECT_DEFAULT;
        options.lock_timeout_ms = 2000;
        output.struct_size = (uint)sizeof(DISDK_DeviceInfoV2);
        output.abi_version = DISDK_ABI_VERSION;

        CopyAscii(options.id_namespace, 65, "com.company.product");
        CopyAscii(options.app_version, 64, "1.0.0");

        int code = DISDK_GetDeviceInfoV2(&options, &output);
        Console.WriteLine($"result_code={code}");
        Console.WriteLine($"device_id={ReadAscii(output.device_id, 64)}");
        return code == 0 ? 0 : 1;
    }
}
