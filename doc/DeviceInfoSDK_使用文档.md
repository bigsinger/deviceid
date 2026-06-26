# Windows DeviceInfo SDK 使用文档

本文档说明 `deviceid-win` 中 DeviceInfoSDK 的实现方式、源码接入方式，以及 demo 工具 `deviceId-win.exe` 的使用方式。

## 1. 功能概览

DeviceInfoSDK 是一个 Windows 桌面端设备信息采集 SDK。它使用 Win32 API 获取系统、硬件、网络和当前用户维度的信息，并生成一个稳定的 `device_id`。

`device_id` 不是硬件序列号，也不是安全认证凭据。它是“当前 Windows 用户 + 当前机器 + namespace”范围内的随机 UUIDv4，并通过本地缓存保持稳定。

### 1.1 device_id 缓存机制

SDK 首次运行时生成 UUIDv4，之后优先读取本地缓存。为了提升稳定性，SDK 使用“双存储 + 修复”机制：

| 存储位置 | 路径/键值 | 用途 |
|---|---|---|
| 注册表主位置 | `HKEY_CURRENT_USER_LOCAL_SETTINGS\Software\DeviceInfoSDK\Namespaces\<namespace>\DeviceId` | 当前用户、本机范围的主缓存位置 |
| 注册表兼容位置 | `HKEY_CURRENT_USER\Software\DeviceInfoSDK\Namespaces\<namespace>\DeviceId` | 当主位置不可用时的稳定性兜底 |
| 文件位置 | `%LOCALAPPDATA%\DeviceInfoSDK\<namespace>\device_id` | 与注册表互相校验和修复 |

读取规则：

| 情况 | 行为 |
|---|---|
| 注册表和文件都有相同合法 UUID | 直接返回该 UUID |
| 只有注册表合法 | 返回注册表 UUID，并回写修复文件 |
| 只有文件合法 | 返回文件 UUID，并回写修复注册表 |
| 两边都有但不同 | 注册表优先，返回注册表 UUID，并修复文件 |
| 两边都没有 | 生成新 UUID，并写入注册表和文件 |

为了避免并发首次运行生成多个 ID，SDK 会按 namespace 加锁：

- 进程内使用 `std::timed_mutex`。
- 跨进程优先使用命名 mutex。
- 命名 mutex 不可用时，退到 `%LOCALAPPDATA%\DeviceInfoSDK\<namespace>\device_id.lock` 文件锁。

### 1.2 采集字段

| 字段 | 默认 demo 输出 | SDK 默认采集 | 实现方式 |
|---|---:|---:|---|
| `platform` | 是 | 是 | 固定 `windows` |
| `device_id` | 是 | 是 | UUIDv4 + 注册表/LocalAppData 双存储 |
| `app_version` | 是 | 是 | 调用方传入；为空时使用 SDK 版本 |
| `app_channel` | 是 | 否 | 调用方传入 |
| `os` | 是 | 是 | 固定 `Windows` |
| `os_version` | 是 | 是 | 动态调用 `RtlGetVersion` |
| `screen_width` / `screen_height` | 是 | 是 | `EnumDisplaySettingsW` 获取主显示器当前模式 |
| `brand` / `model` | 是 | 是 | `GetSystemFirmwareTable('RSMB')` 解析 SMBIOS Type 1 |
| `mac` | 是 | 否 | `GetAdaptersAddresses` 选择活动网卡 |
| `cpuid` | 是 | 否 | `__cpuid` 获取 CPU vendor/family/model/stepping |
| `cpu_model` | 是 | 是 | 注册表 `ProcessorNameString` |
| `cpu_cores` | 是 | 是 | `GetActiveProcessorCount` 活动逻辑处理器数 |
| `memory_gb` | 是 | 是 | `GlobalMemoryStatusEx`，按 GiB 向上取整，32G 机器显示 32 |
| `storage_gb` | 是 | 是 | 枚举所有固定本地磁盘，累计总容量并按 GiB 向上取整 |
| `network_type` | 是 | 是 | 活动网卡类型：`ethernet` / `wifi` / `unknown` |
| `lang` | 是 | 是 | `GetUserDefaultUILanguage` + `LCIDToLocaleName` |
| `hostname` | 是 | 否 | `GetComputerNameExW` |
| `os_username` | 是 | 否 | `GetUserNameW` |

说明：

- SDK 默认仍遵循最小化采集，不自动采集 `mac/cpuid/hostname/os_username`。
- `deviceId-win.exe` 是 demo，因此默认打开全部字段，便于直接观察输出。
- `memory_gb` 和 `storage_gb` 是便于展示的整数容量，不用于精确计费或资产盘点。

### 1.3 默认文本输出示例

```text
platform=windows
device_id=e3e600d8-c563-42c2-be92-1a31f939b731
app_version=2.0.0
app_channel=
os=Windows
os_version=10.0.26200
screen_width=1920
screen_height=1080
model=
brand=
mac=60:1F:FF:2C:6D:CC
cpuid=GenuineIntel-family06-model0a5-stepping5-eax000a0655
cpu_model=Intel(R) Core(TM) i7-10700 CPU @ 2.90GHz
cpu_cores=16
memory_gb=32
storage_gb=4680
network_type=wifi
lang=zh-CN
hostname=PC-BJ
os_username=bob
result_code=1
present_mask=0x00000000000ffff7
error_mask=0x0000000000000300
diagnostic_flags=0x0000000000000000
native_error=1
```

`result_code=1` 表示部分字段失败但主要信息可用。常见原因是某些机器或虚拟环境无法读取 SMBIOS 品牌/型号。

### 1.4 JSON 输出示例

```json
{
  "platform": "windows",
  "device_id": "e3e600d8-c563-42c2-be92-1a31f939b731",
  "app_version": "2.0.0",
  "app_channel": "",
  "os": "Windows",
  "os_version": "10.0.26200",
  "screen_width": 1920,
  "screen_height": 1080,
  "model": "",
  "brand": "",
  "mac": "60:1F:FF:2C:6D:CC",
  "cpuid": "GenuineIntel-family06-model0a5-stepping5-eax000a0655",
  "cpu_model": "Intel(R) Core(TM) i7-10700 CPU @ 2.90GHz",
  "cpu_cores": 16,
  "memory_gb": 32,
  "storage_gb": 4680,
  "network_type": "wifi",
  "lang": "zh-CN",
  "hostname": "PC-BJ",
  "os_username": "bob",
  "_status": {
    "result_code": 1,
    "present_mask": "0x00000000000ffff7",
    "error_mask": "0x0000000000000300",
    "diagnostic_flags": "0x0000000000000000",
    "native_error": 1
  }
}
```

## 2. 源码接入方式

当前工程不是只提供一个头文件。开发者需要把公共头文件和实现源码一起加入自己的工程。

### 2.1 必须复制的文件

复制以下目录到目标工程，例如放到 `third_party/DeviceInfoSDK/`：

```text
deviceid-win/include/DeviceInfoSDK/
deviceid-win/src/
```

必须包含这些公共头文件：

```text
include/DeviceInfoSDK/DeviceInfo.h
include/DeviceInfoSDK/DeviceInfoC.h
```

必须包含这些实现文件：

```text
src/DeviceInfo.cpp
src/DeviceInfoC.cpp
src/internal/DeviceIdManager.cpp
src/internal/NetworkCollector.cpp
src/internal/SmbiosCollector.cpp
src/internal/SystemCollectors.cpp
src/internal/Utf8.cpp
src/internal/DeviceIdManager.h
src/internal/Interfaces.h
src/internal/NetworkCollector.h
src/internal/SmbiosCollector.h
src/internal/SystemCollectors.h
src/internal/Utf8.h
src/internal/WinHandle.h
```

### 2.2 Visual Studio 工程配置

在目标 `.vcxproj` 中加入 include 路径：

```text
third_party/DeviceInfoSDK/include
third_party/DeviceInfoSDK/src
```

推荐预处理宏：

```text
WIN32_LEAN_AND_MEAN
NOMINMAX
UNICODE
_UNICODE
_WIN32_WINNT=0x0601
WINVER=0x0601
NTDDI_VERSION=0x06010000
```

推荐编译选项：

```text
/std:c++17 /utf-8 /W4 /permissive- /sdl
```

需要链接的系统库：

```text
bcrypt.lib
iphlpapi.lib
advapi32.lib
user32.lib
shell32.lib
ole32.lib
ws2_32.lib
```

### 2.3 CMake 接入示例

```cmake
add_library(DeviceInfoSDK STATIC
    third_party/DeviceInfoSDK/src/DeviceInfo.cpp
    third_party/DeviceInfoSDK/src/DeviceInfoC.cpp
    third_party/DeviceInfoSDK/src/internal/DeviceIdManager.cpp
    third_party/DeviceInfoSDK/src/internal/NetworkCollector.cpp
    third_party/DeviceInfoSDK/src/internal/SmbiosCollector.cpp
    third_party/DeviceInfoSDK/src/internal/SystemCollectors.cpp
    third_party/DeviceInfoSDK/src/internal/Utf8.cpp
)

target_include_directories(DeviceInfoSDK PUBLIC
    third_party/DeviceInfoSDK/include
)

target_include_directories(DeviceInfoSDK PRIVATE
    third_party/DeviceInfoSDK/src
)

target_link_libraries(DeviceInfoSDK PUBLIC
    bcrypt
    iphlpapi
    advapi32
    user32
    shell32
    ole32
    ws2_32
)
```

## 3. C++ SDK 使用

### 3.1 最小接入

```cpp
#include <DeviceInfoSDK/DeviceInfo.h>

DeviceInfoSDK::DeviceInfoOptions options;
DeviceInfoSDK::DeviceInfoResult result = DeviceInfoSDK::GetDeviceInfo(options);

if (result.code == DeviceInfoSDK::ResultCode::kOk ||
    result.code == DeviceInfoSDK::ResultCode::kPartial) {
    std::string device_id = result.info.device_id;
}
```

最小接入不需要设置 namespace 和 app_version：

| 参数 | 默认值 |
|---|---|
| `id_namespace` | `com.bigsinger.deviceid` |
| `app_version` | SDK 版本号，例如 `2.0.0` |

生产环境建议显式设置 namespace，避免多个产品共享一个安装标识：

```cpp
DeviceInfoSDK::DeviceInfoOptions options;
options.id_namespace = "com.company.product";
options.app_version = "1.0.0";
```

### 3.2 开启敏感字段

SDK 默认不采集 `mac/cpuid/hostname/os_username`。如需采集：

```cpp
DeviceInfoSDK::DeviceInfoOptions options;
options.collection_flags = DeviceInfoSDK::kCollectDefault |
    DeviceInfoSDK::kCollectMac |
    DeviceInfoSDK::kCollectCpuid |
    DeviceInfoSDK::kCollectHostname |
    DeviceInfoSDK::kCollectUsername;

DeviceInfoSDK::DeviceInfoResult result = DeviceInfoSDK::GetDeviceInfo(options);
```

## 4. C ABI / FFI 使用

```c
#include <DeviceInfoSDK/DeviceInfoC.h>

DISDK_OptionsV2 options;
DISDK_DeviceInfoV2 output;
memset(&options, 0, sizeof(options));
memset(&output, 0, sizeof(output));

options.struct_size = sizeof(options);
options.abi_version = DISDK_ABI_VERSION;
options.collection_flags = DISDK_COLLECT_DEFAULT;
options.lock_timeout_ms = 2000;

output.struct_size = sizeof(output);
output.abi_version = DISDK_ABI_VERSION;

int code = DISDK_GetDeviceInfoV2(&options, &output);
```

如需敏感字段：

```c
options.collection_flags = DISDK_COLLECT_DEFAULT |
    DISDK_COLLECT_MAC |
    DISDK_COLLECT_CPUID |
    DISDK_COLLECT_HOSTNAME |
    DISDK_COLLECT_USERNAME;
```

## 5. deviceinfo_cli 使用

`deviceid-win` 是 demo 工程，生成的可执行文件名为：

```text
deviceId-win.exe
```

默认直接打印所有维度信息：

```powershell
.\deviceId-win.exe
```

输出 JSON：

```powershell
.\deviceId-win.exe -e json
```

CLI 已经默认开启全部字段，不再需要传 namespace、app-version、敏感字段开关等参数。

## 6. 构建命令

使用本机 Visual Studio：

```powershell
& "D:\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.exe" `
  "F:\bigsinger\deviceid\deviceid-win\deviceId-win.vcxproj" `
  /Build "Release|x64"
```

使用 MSBuild：

```powershell
& "D:\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" `
  "F:\bigsinger\deviceid\deviceid-win\deviceId-win.vcxproj" `
  /p:Configuration=Release /p:Platform=x64 /m /v:minimal
```

Win32 构建：

```powershell
& "D:\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" `
  "F:\bigsinger\deviceid\deviceid-win\deviceId-win.vcxproj" `
  /p:Configuration=Release /p:Platform=Win32 /m /v:minimal
```
