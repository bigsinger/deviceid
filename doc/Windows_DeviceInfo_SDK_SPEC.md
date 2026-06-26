# Windows 设备信息获取 SDK 开发规范文档（SPEC）V2

**版本**：2.0  
**日期**：2026-06-26  
**状态**：待评审 / 开发基线  
**替代版本**：V1.0  
**目标平台**：Windows Desktop（Win32），x86 / x64  

---

## 0. 文档说明与规范用语

本文档定义一个轻量级 Windows 设备信息获取 SDK 的完整实现规范。SDK 使用 C++ 编写，只依赖 Windows 系统 API，不进行网络请求，不包含业务上报、JSON 序列化、账号体系、风控策略或服务端逻辑。

本文档中的规范用语：

- **必须（MUST）**：实现和验收不可省略。
- **禁止（MUST NOT）**：实现中不得出现。
- **应该（SHOULD）**：默认必须执行，只有明确、可记录的理由才可偏离。
- **可以（MAY）**：可选能力，不影响基础验收。

V2 是新的开发基线；V1 中与本文件冲突的内容，以 V2 为准。V1 引用的 `deviceid.md` 未随本次输入提供，因此 V2 已将字段契约、取值、空值、隐私属性及验收规则完整内嵌，不再依赖该外部文档。

---

## 1. V1 审查结论与 V2 修订摘要

| 优先级 | V1 问题 | 风险 | V2 修订 |
|---|---|---|---|
| P0 | 对外只暴露含 `std::string` 的 C++ 接口，同时声称可供 FFI 调用 | C++ ABI、运行时库和内存所有权不稳定，跨语言/跨编译器容易崩溃 | 保留 C++ 便利接口，同时新增固定布局、调用方分配内存的 C ABI |
| P0 | `device_id` 使用 HKCU 与用户目录，却描述为“全局设备 ID” | 实际作用域不清，漫游用户配置还可能把同一 ID 带到其他机器 | 明确定义为“当前 Windows 用户 + 当前本机 + namespace”的伪匿名安装标识；改用非漫游注册表与 LocalAppData |
| P0 | 仅要求 `UuidCreate`，但没有校验 UUID 的版本位、变体位和标准格式 | 无法严格保证输出为规范 UUIDv4 | 使用 `BCryptGenRandom` 获取 128 位随机数，显式设置 RFC 9562 的 v4/variant 位，并做严格校验 |
| P0 | 双持久化没有内容校验、冲突处理、跨线程/跨进程锁和原子文件写入 | 并发首次启动可能生成两个 ID；崩溃可能留下半写文件 | 增加 namespace 锁、校验/归一化、冲突矩阵、临时文件 + 原子替换、修复状态 |
| P0 | `GetAdaptersInfo` 方案与“按 OperStatus 选择活动网卡”的要求不一致，并且仅覆盖 IPv4 旧接口 | 网络类型和 MAC 可能选到离线、回环、隧道或错误适配器 | 使用 `GetAdaptersAddresses(AF_UNSPEC)`；结合 `GetIfEntry2`、网关、活动状态和接口指标进行确定性选择 |
| P0 | 全部错误静默变成空值，调用方无法区分“未请求”“不支持”“失败” | 难以监控质量、定位兼容性问题 | 增加 `ResultCode`、`present_mask`、`error_mask`、`diagnostic_flags` 和首个原生错误码 |
| P0 | MAC、主机名、用户名默认采集 | 隐私合规与最小化原则风险 | 三项默认关闭，必须由调用方显式开启 |
| P1 | `GetSystemMetrics` 的分辨率语义没有说明 DPI 和多显示器 | DPI 非感知进程可能获得逻辑像素，结果不稳定 | 主路径改为 `EnumDisplaySettingsW(..., ENUM_CURRENT_SETTINGS, ...)`，定义为主显示器当前物理像素 |
| P1 | `cpu_cores` 实际取逻辑处理器数，名称与语义容易误解 | 大于 64 逻辑处理器或处理器组场景不准确 | 明确字段语义为活动逻辑处理器数，使用 `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)` |
| P1 | `model`、`brand` 因“不使用 WMI”而固定为空 | 可用性不足 | 使用 `GetSystemFirmwareTable('RSMB')` 解析 SMBIOS Type 1，无需 WMI/COM |
| P1 | `_gb` 字段直接除以 `1024^3`，但文档称 GB | 单位歧义；`int` 在极大容量下不稳健 | 保留既有字段名，但明确语义为 GiB 向下取整，类型改为 `uint64_t` |
| P1 | “静态内部函数便于单元测试”与实际可测试性相反 | 难以注入注册表、文件、随机源和系统 API 失败 | 内部采用接口/函数表测试缝，生产实现与测试替身分离 |
| P1 | JSON 模式在普通输出后追加 JSON | 自动化工具无法把 stdout 当作纯 JSON 解析 | `--format json` / `-e json` 模式只向 stdout 输出一个 JSON 对象 |
| P1 | Windows 7、VS2015 和当前工具链状态未区分 | 使用失去支持的工具链和系统形成供应链风险 | Windows 7 标记为“兼容性目标而非安全支持目标”；发布基线固定为 VS2022，不使用 VS2026 构建 Win7 版本 |

---

## 2. 项目目标、范围与非目标

### 2.1 项目目标

SDK 必须实现：

1. 在 Win32 桌面进程中采集统一、可判定质量的设备信息。
2. 生成和持久化符合 RFC 9562 的 UUIDv4 `device_id`。
3. 提供 C++ API 与稳定的 C ABI，支持原生集成及 FFI。
4. 不依赖 WMI、COM 初始化、第三方运行库或网络服务。
5. 单项失败时尽力返回其他字段，并向调用方提供字段级状态。
6. 默认执行数据最小化，敏感字段必须显式开启。

### 2.2 支持范围

- Windows Desktop / Win32 应用。
- x86、x64 为必须发布架构。
- Windows 7 SP1 为可选兼容性目标；Windows 10/11 为主要运行目标。
- 普通桌面用户进程，以及以固定服务账号运行、未切换模拟用户的服务进程。
- 静态库与可选 DLL。

### 2.3 非目标

SDK 不负责：

- 生成 JSON、XML、Protobuf 或执行 HTTP 上报。
- 识别真实自然人、Windows 账号主体或企业账号。
- 提供不可伪造的硬件身份、反作弊证明、许可证硬件锁或可信设备认证。
- 跨用户得到同一个机器级 ID。
- 在重装系统、删除用户配置、还原快照、非持久 VDI 后保证 ID 不变。
- 绕过沙箱、组策略、权限控制或安全软件。
- 采集序列号、磁盘序列号、主板序列号、TPM 密钥等高敏感硬件指纹。
- 支持 UWP、Windows AppContainer 或内核驱动。
- 自动记录日志、上传遥测或持久运行后台线程。

---

## 3. 关键语义与产品决策

### 3.1 `device_id` 的准确含义

V2 中的 `device_id` 是：

> **当前 Windows 用户、当前本机、指定 `id_namespace` 下的随机持久化标识。**

它不是主板 ID、CPU ID、Windows Product ID，也不是全局机器 ID。

稳定性边界：

- 同一台机器、同一 Windows 用户、同一 `id_namespace`：应稳定。
- 同一台机器、不同 Windows 用户：默认不同。
- 不同机器、相同漫游域账号：应不同；V2 使用非漫游注册表和 `%LOCALAPPDATA%` 降低跨机器漫游风险。
- 删除两处持久化数据、删除用户配置、重装系统或恢复旧快照：可能变化。
- 多个应用只有在明确配置同一个 `id_namespace` 时才共享 ID。

### 3.2 `id_namespace`

`id_namespace` 必须由集成方在生产构建前确定，推荐反向域名格式，例如：

```text
com.example.product
```

验证规则：

```regex
^[a-z0-9][a-z0-9._-]{2,63}$
```

要求：

- 长度 3～64 字节。
- 只允许小写 ASCII 字母、数字、点、下划线、连字符。
- 不得包含斜杠、反斜杠、冒号、空格、控制字符或路径跳转片段。
- 空值、占位值 `com.example.product`、不合法值必须返回 `kInvalidArgument`，不得落盘。

### 3.3 数据可信边界

注册表和文件均可由当前用户修改，因此：

- `device_id`、MAC、主机名和用户名均可被用户或本机恶意程序伪造。
- 禁止把这些字段当作认证秘密、加密密钥、支付授权依据或唯一风控证据。
- SDK 保证的是格式、持久化策略和尽力采集，不保证硬件真实性。

---

## 4. 总体架构

```text
调用方
  ├─ C++ API：DeviceInfo.h
  └─ C ABI / FFI：DeviceInfoC.h
          │
          ▼
DeviceInfo Facade
  ├─ 参数校验与结果聚合
  ├─ Device ID Manager
  │    ├─ Namespace Lock
  │    ├─ Registry Store
  │    ├─ Local File Store
  │    └─ Random Source / UUID Codec
  ├─ System Collectors
  │    ├─ OS / Locale
  │    ├─ Display
  │    ├─ SMBIOS
  │    ├─ CPU / Memory / Storage
  │    ├─ Network
  │    └─ Host / User
  └─ UTF-8 / RAII / Bounds Utilities
```

模块必须彼此解耦。设备 ID 持久化逻辑不得散落在字段采集代码中；网络选择必须只执行一次，并同时服务 `network_type` 和 `mac`。

---

## 5. 数据契约

### 5.1 通用约定

- 所有公开字符串均为 UTF-8、无 BOM。
- 字符串失败时返回空字符串，不返回 `"unknown"`，除非字段枚举明确规定该值。
- 数值失败时返回 `0`。
- `present_mask` 表示字段已成功、有效地获取；固定值和合法调用方输入也属于 present。
- `error_mask` 表示字段已请求但采集失败。
- 未请求的可选字段必须为空/0，且不得设置 `error_mask`。
- SDK 不在公开字符串中返回尾随空格、CR/LF 或嵌入 NUL。
- 所有长度限制按 UTF-8 字节数计算。

### 5.2 核心字段

| 字段 | C++ 类型 | 必填 | 默认采集 | 隐私级别 | 精确定义与来源 |
|---|---:|---:|---:|---|---|
| `platform` | `string` | 是 | 是 | 低 | 固定小写 `windows` |
| `device_id` | `string` | 是 | 是 | 中 | 小写、36 字符、带连字符的 UUIDv4；双存储均失败时可能为 `fallback-*`，此时结果码必须为 `kDeviceIdVolatile` |
| `app_version` | `string` | 是 | 调用方输入 | 低 | 从 `DeviceInfoOptions.app_version` 原样复制并校验 UTF-8；必须为 1～63 字节；SDK 不自行读取 EXE 版本 |
| `app_channel` | `string` | 否 | 调用方输入 | 低 | 从 `DeviceInfoOptions.app_channel` 复制；允许空 |
| `os` | `string` | 是 | 是 | 低 | 固定 `Windows` |
| `os_version` | `string` | 是 | 是 | 低 | `major.minor.build`，使用动态加载的 `RtlGetVersion` |
| `screen_width` | `uint32_t` | 是 | 是 | 低 | 主显示器当前显示模式的物理像素宽度 |
| `screen_height` | `uint32_t` | 是 | 是 | 低 | 主显示器当前显示模式的物理像素高度 |
| `model` | `string` | 否 | 是 | 中 | SMBIOS System Information（Type 1）的 Product Name；无效占位值转为空 |
| `brand` | `string` | 否 | 是 | 中 | SMBIOS Type 1 的 Manufacturer；无效占位值转为空 |
| `mac` | `string` | 否 | **否** | 高 | 选中网络适配器的 6 字节地址，格式 `XX:XX:XX:XX:XX:XX`；仅显式启用后采集 |
| `cpuid` | `string` | 否 | 否 | 高 | V2 保留字段，固定为空；不得伪造 CPU 序列号 |
| `cpu_model` | `string` | 否 | 是 | 低 | 注册表 `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0\ProcessorNameString`，去除首尾及连续多余空白 |
| `cpu_cores` | `uint32_t` | 否 | 是 | 低 | **活动逻辑处理器数**，不是物理核心数；跨全部处理器组统计 |
| `memory_gb` | `uint64_t` | 否 | 是 | 低 | `floor(total_physical_bytes / 2^30)`；字段名保留 `_gb`，实际单位为 GiB |
| `storage_gb` | `uint64_t` | 否 | 是 | 低 | Windows 系统目录所在卷的总容量，`floor(total_bytes / 2^30)`，实际单位为 GiB |
| `network_type` | `string` | 否 | 是 | 低 | 精确枚举：`ethernet`、`wifi`、`unknown`；与 `mac` 使用同一选中适配器 |
| `lang` | `string` | 否 | 是 | 低 | 当前用户默认 UI 语言的 BCP 47/locale 名称，例如 `zh-CN` |
| `hostname` | `string` | 否 | **否** | 高 | `ComputerNamePhysicalDnsHostname`；仅显式启用后采集 |
| `os_username` | `string` | 否 | **否** | 高 | 当前安全上下文用户名；服务中可能是服务账号，仅显式启用后采集 |

### 5.3 SMBIOS 占位值过滤

`brand`、`model` 对下列大小写不敏感的值必须转为空：

```text
To be filled by O.E.M.
Default string
System Product Name
Not Specified
Not Applicable
Unknown
None
N/A
```

纯空白、全 NUL、超过公开字段最大长度、包含无法转换为 UTF-8 的字符串也必须视为无效。

### 5.4 字段长度上限

| 字段 | 最大 UTF-8 字节数（不含 NUL） |
|---|---:|
| `id_namespace` | 64 |
| `app_version` | 63 |
| `app_channel` | 63 |
| `device_id` | 63 |
| `os_version` | 31 |
| `brand` | 127 |
| `model` | 255 |
| `cpu_model` | 255 |
| `mac` | 31 |
| `network_type` | 15 |
| `lang` | 31 |
| `hostname` | 255 |
| `os_username` | 255 |

C++ API 遇到超长调用方输入必须返回 `kInvalidArgument`。系统采集值超长时，C++ API 可在 UTF-8 码点边界截断，并设置该字段的 `error_mask` 与 `kDiagOutputTruncated`；C ABI 也必须使用相同规则，且始终 NUL 终止，禁止静默截断。

---

## 6. 采集选项与默认隐私策略

```cpp
enum CollectionFlags : std::uint64_t {
    kCollectHardware     = 1ull << 0, // brand/model/cpu/memory/storage
    kCollectDisplay      = 1ull << 1, // screen width/height
    kCollectNetworkType  = 1ull << 2,
    kCollectLocale       = 1ull << 3,
    kCollectMac          = 1ull << 16, // sensitive, opt-in
    kCollectHostname     = 1ull << 17, // sensitive, opt-in
    kCollectUsername     = 1ull << 18  // sensitive, opt-in
};

constexpr std::uint64_t kCollectDefault =
    kCollectHardware | kCollectDisplay |
    kCollectNetworkType | kCollectLocale;
```

固定字段、OS 版本和 `device_id` 不受采集标志控制，始终尝试获取。

调用方启用敏感字段前必须完成自身的隐私告知、合法性判断、传输保护、访问控制和保留期限设计。SDK 不代替业务合规评审。

---

## 7. C++ 公共 API

文件：`include/DeviceInfoSDK/DeviceInfo.h`

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace DeviceInfoSDK {

constexpr std::uint32_t kAbiVersion = 2;

enum class ResultCode : std::uint32_t {
    kOk = 0,                // 所有已请求字段成功，device_id 已持久化
    kPartial = 1,           // device_id 可用，但至少一个已请求字段失败/被截断/仅单存储成功
    kDeviceIdVolatile = 2,  // device_id 仅本次进程可用，未成功持久化
    kInvalidArgument = 3,   // namespace、UTF-8、长度或结构参数非法
    kBusy = 4,              // 获取 namespace 锁超时
    kInternalError = 5      // 未预期内部错误，仍应尽量返回已采集字段
};

enum DiagnosticFlags : std::uint64_t {
    kDiagNone                  = 0,
    kDiagDeviceIdGenerated     = 1ull << 0,
    kDiagRegistryRepaired      = 1ull << 1,
    kDiagFileRepaired          = 1ull << 2,
    kDiagStoreConflict         = 1ull << 3,
    kDiagLegacyMigrated        = 1ull << 4,
    kDiagFallbackId            = 1ull << 5,
    kDiagOutputTruncated       = 1ull << 6,
    kDiagDisplayFallback       = 1ull << 7,
    kDiagVirtualAdapterPossible= 1ull << 8
};

struct DeviceInfoOptions {
    std::string id_namespace;       // 必填，例如 com.company.product
    std::string app_version;        // 业务要求必填；最大 63 字节
    std::string app_channel;        // 可空；最大 63 字节
    std::uint64_t collection_flags = kCollectDefault;
    std::uint32_t lock_timeout_ms = 2000;
    bool enable_legacy_migration = false;
};

struct DeviceInfo {
    std::string platform;
    std::string device_id;
    std::string app_version;
    std::string app_channel;
    std::string os;
    std::string os_version;
    std::uint32_t screen_width = 0;
    std::uint32_t screen_height = 0;
    std::string model;
    std::string brand;
    std::string mac;
    std::string cpuid;
    std::string cpu_model;
    std::uint32_t cpu_cores = 0;
    std::uint64_t memory_gb = 0;
    std::uint64_t storage_gb = 0;
    std::string network_type;
    std::string lang;
    std::string hostname;
    std::string os_username;
};

struct DeviceInfoResult {
    DeviceInfo info;
    ResultCode code = ResultCode::kInternalError;
    std::uint64_t present_mask = 0;
    std::uint64_t error_mask = 0;
    std::uint64_t diagnostic_flags = 0;
    std::uint32_t native_error = 0; // 第一个具有诊断价值的 Win32/NTSTATUS 错误
};

DeviceInfoResult GetDeviceInfo(const DeviceInfoOptions& options) noexcept;

// 仅用于卸载、隐私删除或明确重置流程；正常运行不得调用。
ResultCode DeletePersistedDeviceId(
    const DeviceInfoOptions& options,
    std::uint32_t* native_error = nullptr) noexcept;

const char* GetSdkVersion() noexcept; // 返回静态 ASCII 字符串，例如 "2.0.0"

} // namespace DeviceInfoSDK
```

### 7.1 C++ ABI 约束

包含 `std::string` 的 C++ 接口只保证在以下条件下安全：

- SDK 与调用方使用兼容的 MSVC 工具集、运行时库和编译选项。
- 不跨不兼容的编译器、CRT 或模块边界释放彼此分配的 C++ 对象。
- DLL/FFI 场景必须优先使用第 8 节 C ABI。

### 7.2 异常约定

- 所有公开函数必须标记 `noexcept`。
- API 边界必须捕获 `std::exception` 和未知 C++ 异常，并返回 `kInternalError`。
- 不承诺捕获访问冲突、栈溢出、进程终止或无法恢复的全局内存耗尽。

---

## 8. C ABI / FFI 接口

文件：`include/DeviceInfoSDK/DeviceInfoC.h`

```c
#pragma once
#include <stdint.h>

#if defined(_WIN32) && defined(DISDK_BUILD_DLL)
#  define DISDK_API __declspec(dllexport)
#elif defined(_WIN32) && defined(DISDK_USE_DLL)
#  define DISDK_API __declspec(dllimport)
#else
#  define DISDK_API
#endif

#if defined(_MSC_VER)
#  define DISDK_CALL __cdecl
#else
#  define DISDK_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DISDK_ABI_VERSION 2u

#pragma pack(push, 8)

typedef struct DISDK_OptionsV2 {
    uint32_t struct_size;      /* caller: sizeof(DISDK_OptionsV2) */
    uint32_t abi_version;      /* caller: DISDK_ABI_VERSION */
    uint64_t collection_flags;
    uint32_t lock_timeout_ms;
    uint32_t option_flags;     /* bit0: enable_legacy_migration */
    char id_namespace[65];
    char app_version[64];
    char app_channel[64];
} DISDK_OptionsV2;

typedef struct DISDK_DeviceInfoV2 {
    uint32_t struct_size;      /* caller: sizeof(DISDK_DeviceInfoV2) */
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
```

### 8.1 C ABI 规则

1. 调用方必须将结构体清零，再设置 `struct_size` 和 `abi_version`。
2. SDK 只能写入 `min(caller.struct_size, sizeof(current_struct))` 范围，支持未来尾部扩展。
3. `abi_version != 2`、结构体小于 V2 最小必需偏移、空指针或缺少 NUL 终止时返回 `kInvalidArgument`。
4. 所有输出缓冲区始终 NUL 终止。
5. 不返回需要调用方释放的堆内存；不传递 C++ 对象或异常。
6. 导出调用约定固定为 `__cdecl`。
7. 发布包必须包含 x86/x64 结构偏移自动测试，以及供 Rust、C# P/Invoke、Python `ctypes` 至少各一个最小示例。

---

## 9. Device ID 设计

### 9.1 持久化位置

#### 注册表（本机非漫游、当前用户）

```text
Root: HKEY_CURRENT_USER_LOCAL_SETTINGS
Key : Software\DeviceInfoSDK\Namespaces\<id_namespace>
Name: DeviceId
Type: REG_SZ
```

#### 文件（本机非漫游、当前用户）

```text
%LOCALAPPDATA%\DeviceInfoSDK\<id_namespace>\device_id
```

文件内容为小写 UUIDv4 加一个 LF：

```text
xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx\n
```

- UTF-8，无 BOM。
- 读取上限 128 字节；超过上限视为损坏。
- 注册表读取上限 128 个 UTF-16 代码单元；类型不是 `REG_SZ`/`REG_EXPAND_SZ` 时视为无效。
- 目录使用 `SHGetKnownFolderPath(FOLDERID_LocalAppData)` 获取；禁止依赖 `%USERPROFILE%` 字符串拼接。

### 9.2 UUIDv4 生成

必须执行：

```cpp
std::uint8_t bytes[16];
BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40); // version 4
bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80); // RFC variant 10
```

随后编码为小写规范文本。禁止把 `UuidCreate` 的返回文本不经校验直接作为 V2 主实现。

### 9.3 严格校验

有效 ID 必须同时满足：

- 长度恰为 36。
- 连字符位于索引 8、13、18、23。
- 其余字符全部是十六进制。
- 第 14 个字符为 `4`（版本 4）。
- 第 19 个字符为 `8`、`9`、`a` 或 `b`（RFC variant）。
- 读取时允许大写，返回和回写时统一转为小写。
- 读取时可去除首尾 ASCII 空白；嵌入空白、BOM、NUL 或附加内容必须判无效。

### 9.4 并发控制

同一 `id_namespace` 的读取、生成、修复、删除必须在同一个锁内完成。

- 进程内：`std::mutex` 或等价锁。
- 跨进程/跨会话：命名 mutex，名称由“当前用户 SID + namespace”的 SHA-256 前 128 位构造：

```text
Global\DeviceInfoSDK-<32 lowercase hex>
```

- mutex 的 DACL 应限制为当前用户和 SYSTEM。
- 若全局命名 mutex 因安全策略、对象抢占或 ACL 无法创建/打开，必须回退到 `%LOCALAPPDATA%\DeviceInfoSDK\<id_namespace>\device_id.lock`：使用 `CreateFileW(OPEN_ALWAYS)` 且共享模式为 0，持有句柄期间视为加锁；进程崩溃后系统会释放句柄。
- 默认等待 2000 ms，范围 0～30000 ms。
- 得到锁后必须重新读取两处存储，禁止使用加锁前的缓存结论。
- 遇到 `WAIT_ABANDONED` 可继续，但必须重新完整校验和修复。
- 锁超时不得写入；若已有一个有效持久 ID，可返回该 ID 和 `kBusy`；否则返回临时 ID 和 `kDeviceIdVolatile`，并设置原生错误。

### 9.5 读取、冲突与修复矩阵

注册表优先级高于文件，但两者都必须校验：

| 注册表 | 文件 | 返回值 | 修复动作 | 诊断标记 |
|---|---|---|---|---|
| 有效 A | 有效 A | A | 无 | 无 |
| 有效 A | 缺失/无效 | A | 原子重写文件 | `kDiagFileRepaired` |
| 缺失/无效 | 有效 B | B | 写入注册表 | `kDiagRegistryRepaired` |
| 有效 A | 有效 B，A≠B | A | 注册表胜出，原子重写文件 | `kDiagStoreConflict | kDiagFileRepaired` |
| 缺失/无效 | 缺失/无效 | 新 UUID | 双写 | `kDiagDeviceIdGenerated` |

只要发生修复或任一存储写入失败，结果码至少为 `kPartial`。

### 9.6 写入规则

#### 注册表

- 使用 `RegCreateKeyExW` + `RegSetValueExW`。
- 只写规范小写 UUIDv4。
- 不调用高开销的 `RegFlushKey` 作为常规路径。

#### 文件

1. 确保 namespace 目录存在。
2. 在同一目录创建唯一临时文件，例如 `device_id.tmp.<pid>.<counter>`。
3. 使用独占写入，写入完整内容。
4. 调用 `FlushFileBuffers`。
5. 关闭临时文件句柄。
6. 使用 `MoveFileExW(temp, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` 替换。
7. 失败时尽力删除临时文件。

禁止直接以截断模式覆盖目标文件。

### 9.7 持久化失败

- 新 UUID 双写成功：`kOk`。
- 仅一处写入成功：返回 UUID，`kPartial`；后续调用应尝试修复另一处。
- 两处都失败：返回临时 ID，`kDeviceIdVolatile`，设置 `kDiagFallbackId`，不得把 fallback 写入任何存储。
- 随机源失败时临时格式：

```text
fallback-<unix_ms>-<pid>-<tid>-<monotonic_counter>
```

fallback 只用于保证返回非空，不具备 UUID、安全随机性或跨进程稳定性。

### 9.8 显式删除

`DeletePersistedDeviceId`：

- 只用于卸载、用户隐私删除、测试清理或业务明确的设备重置流程。
- 必须持有同一 namespace 锁。
- 删除注册表值和文件；目录为空时可删除目录。
- 任一位置删除失败返回 `kPartial`。
- 下一次正常调用会生成新 UUID。
- 正常应用启动、升级和退出路径禁止自动调用。

### 9.9 V1 迁移

仅当 `enable_legacy_migration=true` 且 V2 两处均无有效 ID 时，才可读取：

```text
HKCU\Software\MyCompany\MyApp\DeviceId
%USERPROFILE%\.deviceid
```

迁移值必须通过 V2 UUIDv4 严格校验；合法后写入 V2 两处，设置 `kDiagLegacyMigrated`。迁移后默认不删除旧数据。新项目默认关闭迁移。

---

## 10. 系统信息采集详细设计

### 10.1 OS 版本

函数：`CollectOsVersion()`

- 使用 `GetModuleHandleW(L"ntdll.dll")` 和 `GetProcAddress("RtlGetVersion")`。
- 初始化 `RTL_OSVERSIONINFOW.dwOSVersionInfoSize`。
- 输出 `major.minor.build`，不附加产品名称。
- 不使用可能受应用清单影响而返回兼容版本的旧版版本判断逻辑作为主路径。
- 失败则 `os_version=""`，设置字段错误位。

### 10.2 屏幕分辨率

函数：`CollectPrimaryDisplayMode()`

主路径：

```cpp
DEVMODEW dm{};
dm.dmSize = sizeof(dm);
EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm);
```

- 使用 `dmPelsWidth`、`dmPelsHeight`。
- 定义为当前线程所在桌面的主显示器当前模式、物理像素。
- 不返回虚拟桌面总尺寸。
- 远程桌面中返回远程会话当前显示模式。
- 失败时可回退 `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)`，并设置 `kDiagDisplayFallback`；回退值可能受 DPI 感知上下文影响。

### 10.3 品牌与型号

函数：`CollectSmbiosSystemInfo()`

- 使用显式 little-endian FourCC `R | (S<<8) | (M<<16) | (B<<24)` 作为 `RSMB` provider signature，调用 `GetSystemFirmwareTable(provider, 0, nullptr, 0)` 获取所需大小；禁止依赖实现相关的多字符字面量数值。
- 大小必须大于 `sizeof(RawSMBIOSData)` 且不超过 1 MiB。
- 第二次调用后验证返回长度、`RawSMBIOSData.Length` 和总缓冲边界。
- 遍历 SMBIOS 结构，找到 Type 1。
- Type 1 格式区最小长度必须允许访问：
  - offset `0x04`：Manufacturer 字符串索引。
  - offset `0x05`：Product Name 字符串索引。
- 必须正确跳过格式区及其后的双 NUL 字符串区。
- 每个结构、字符串和索引均做边界检查；遇到损坏表立即停止，不允许越界。
- SMBIOS 文本按规范视为 UTF-8、无 BOM；先做严格 UTF-8 校验，再执行长度和占位值过滤。US-ASCII 自然属于有效 UTF-8。
- 可对解析器执行 fuzz 测试。

### 10.4 CPU 型号与逻辑处理器

#### CPU 型号

- 读取 `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0` 的 `ProcessorNameString`。
- 接受 `REG_SZ`；最大 512 个 UTF-16 代码单元。
- 去除首尾空白，将连续空白压缩为单个 ASCII 空格。

#### `cpu_cores`

- 主路径：`GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)`。
- 返回值语义是活动逻辑处理器数。
- 返回 0 时回退 `GetSystemInfo().dwNumberOfProcessors`。
- 该字段名称因上游兼容保留，不得把它解释为物理核心数。

### 10.5 内存

- 使用 `GlobalMemoryStatusEx`。
- `MEMORYSTATUSEX.dwLength` 必须初始化。
- `memory_gb = ullTotalPhys / (1024ull * 1024ull * 1024ull)`。
- 使用 `uint64_t` 防止容量溢出。

### 10.6 系统卷容量

1. `GetWindowsDirectoryW` 获取 Windows 目录。
2. `GetVolumePathNameW` 获取卷根路径，不假设固定为 `C:\`。
3. `GetDiskFreeSpaceExW` 获取 `totalNumberOfBytes`。
4. `storage_gb = total_bytes / 2^30`。

该字段是卷总容量，不是剩余空间，也不是所有磁盘容量之和。

### 10.7 网络适配器选择

#### 获取数据

- 使用 `GetAdaptersAddresses(AF_UNSPEC, flags, ...)`。
- flags 至少包含：
  - `GAA_FLAG_INCLUDE_GATEWAYS`
  - `GAA_FLAG_SKIP_ANYCAST`
  - `GAA_FLAG_SKIP_MULTICAST`
  - `GAA_FLAG_SKIP_DNS_SERVER`
- 初始缓冲区 15 KiB。
- 若返回 `ERROR_BUFFER_OVERFLOW`，按系统给出的大小重分配，最多重试 3 次。
- 缓冲区硬上限 1 MiB。
- 网络查询是同步调用；不得在 SDK 内创建后台线程。

#### 候选过滤

候选必须：

1. `OperStatus == IfOperStatusUp`。
2. 不是 `IF_TYPE_SOFTWARE_LOOPBACK`。
3. 不是 `IF_TYPE_TUNNEL`。
4. 存在至少一个 unicast 地址。
5. 接口类型优先为 `IF_TYPE_ETHERNET_CSMACD` 或 `IF_TYPE_IEEE80211`。

对每个候选使用 `GetIfEntry2` 获取 `MIB_IF_ROW2`：

- 优先 `HardwareInterface == TRUE`。
- 优先 `ConnectorPresent == TRUE`。
- 排除 `NotMediaConnected == TRUE`、`Paused == TRUE`、`EndPointInterface == TRUE`。

上述标志由驱动报告，不能绝对识别全部虚拟适配器；若最终选择的适配器不是明确硬件接口，设置 `kDiagVirtualAdapterPossible`。

#### 确定性排序

按以下顺序选择第一项：

1. 有默认网关优先。
2. 硬件接口优先。
3. 接口类型已知（Ethernet/Wi-Fi）优先。
4. `min(Ipv4Metric, Ipv6Metric)` 较小者优先；不存在的协议指标按最大值处理。
5. `IfIndex` 较小者优先。
6. 永久 `AdapterName` 字节序字典序作为最终稳定打破平局条件。

不得直接依赖 API 返回链表顺序。

#### 输出

- `IfType == IF_TYPE_IEEE80211` → `network_type="wifi"`。
- `IfType == IF_TYPE_ETHERNET_CSMACD` → `network_type="ethernet"`。
- 其他已选类型 → `network_type="unknown"`。
- 无候选或 API 失败 → `network_type="unknown"`；API 失败时设置错误位，无活动适配器但 API 成功不视为错误。
- `mac` 只在 `kCollectMac` 开启且 `PhysicalAddressLength == 6` 时输出。
- 随机化 Wi-Fi MAC 是操作系统当前报告值，SDK 不尝试还原永久硬件地址。

### 10.8 语言

- `GetUserDefaultUILanguage()` 获取 LANGID。
- 转为 LCID 后调用 `LCIDToLocaleName`。
- 输出如 `zh-CN`、`en-US`。
- 失败返回空字符串；禁止自行维护语言映射表。

### 10.9 主机名

- 仅 `kCollectHostname` 开启时调用。
- 使用 `GetComputerNameExW(ComputerNamePhysicalDnsHostname, ...)`。
- 先查询所需长度，再分配缓冲区。
- 不使用固定 `MAX_COMPUTERNAME_LENGTH` 缓冲区假设。

### 10.10 用户名

- 仅 `kCollectUsername` 开启时调用。
- 使用 `GetUserNameW`，按返回所需长度扩容。
- 返回当前安全上下文名称；服务、计划任务或模拟场景可能不是交互式登录用户。
- SDK 不额外查询或拼接域名。

### 10.11 UTF-16 / UTF-8 转换

- 使用 `WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ...)`。
- 使用“两次调用”先获取长度，再分配。
- 所有乘法和长度转换必须检查溢出。
- 无效 UTF-16 必须使当前字段失败，不得输出本地代码页替代值。
- 禁止使用 `codecvt`、`setlocale`、ACP 或依赖进程区域设置的转换。

---

## 11. 错误、状态与诊断

### 11.1 ResultCode 聚合规则

优先级从高到低：

```text
kInvalidArgument
kDeviceIdVolatile
kBusy
kInternalError
kPartial
kOk
```

聚合要求：

- 参数非法时不得执行持久化或敏感采集。
- 只要 `device_id` 为 fallback，必须返回 `kDeviceIdVolatile`。
- 任一已请求可选字段失败、截断、持久化只成功一处或发生可修复冲突，至少返回 `kPartial`。
- 未请求字段为空不导致 `kPartial`。

### 11.2 字段掩码

实现必须在公开头文件中定义每个字段对应的 64 位 bit，且 V2 生命周期内不得重排。建议按数据结构顺序分配 bit 0～19。

- `present_mask & bit != 0`：字段有效。
- `error_mask & bit != 0`：字段已请求但采集或编码失败。
- 同一字段正常情况下不得同时 present 和 error；发生截断时允许同时设置，表示“有可用但不完整的值”。

### 11.3 `native_error`

- 保存第一个最能解释非 OK 结果的 Win32 `GetLastError()`、IP Helper 错误或映射后的 NTSTATUS。
- 没有原生错误时为 0。
- 该字段只用于诊断，不形成稳定业务枚举。
- SDK 不输出系统错误文本，不调用 stderr，不写日志文件。

---

## 12. 线程安全、资源与安全编码

### 12.1 线程/进程安全

- `GetDeviceInfo` 必须可重入、线程安全。
- 不得修改可见的进程全局区域设置、错误模式、当前目录或 DPI 感知状态。
- 除 namespace 锁和只读版本字符串外，不使用可变全局状态。
- 同一调用中网络枚举只执行一次。
- 不创建持久线程、线程池、窗口或消息循环。

### 12.2 资源管理

- HANDLE、HKEY、动态库句柄、CoTaskMem 和堆缓冲区必须由 RAII 包装。
- 所有早退路径必须释放资源。
- 允许使用 `GetModuleHandleW` 获取已加载的 `ntdll.dll`；不得错误调用 `FreeLibrary` 释放该句柄。
- `SHGetKnownFolderPath` 返回的内存使用 `CoTaskMemFree`。

### 12.3 缓冲区与输入防护

- SMBIOS、注册表、文件、适配器列表均设置硬上限。
- 所有系统返回长度都必须与本地缓冲区再次比对。
- 路径只由已验证 namespace 与系统已知目录构造。
- 不接受调用方提供任意注册表路径或文件路径。
- 不跟随调用方指定的路径，因此 V2 不提供自定义持久化目录公共参数。
- 文件/注册表内容损坏只能导致当前字段无效，不得导致崩溃或越界。

### 12.4 隐私与日志

- SDK 默认不采集 MAC、主机名、用户名。
- SDK 不记录 `device_id`、MAC、用户名、主机名或完整文件路径。
- 上层日志如需记录结果，只应记录 `ResultCode`、掩码、诊断标记和脱敏错误码。
- `device_id` 应按伪匿名标识处理，并配置服务端访问控制与删除策略。

---

## 13. 项目目录与内部可测试性

```text
DeviceInfoSDK/
├─ CMakeLists.txt
├─ include/
│  └─ DeviceInfoSDK/
│     ├─ DeviceInfo.h
│     └─ DeviceInfoC.h
├─ src/
│  ├─ DeviceInfo.cpp
│  ├─ DeviceInfoC.cpp
│  └─ internal/
│     ├─ DeviceIdManager.h/.cpp
│     ├─ RegistryStore.h/.cpp
│     ├─ FileStore.h/.cpp
│     ├─ RandomSource.h/.cpp
│     ├─ NetworkCollector.h/.cpp
│     ├─ SmbiosCollector.h/.cpp
│     ├─ SystemCollectors.h/.cpp
│     ├─ Utf8.h/.cpp
│     └─ WinHandle.h
├─ tests/
│  ├─ unit/
│  ├─ integration/
│  ├─ abi/
│  ├─ fuzz/
│  └─ fixtures/
├─ tools/
│  └─ deviceinfo_cli/
├─ examples/
│  ├─ cpp/
│  ├─ c/
│  ├─ csharp/
│  ├─ rust/
│  └─ python/
└─ docs/
   ├─ CHANGELOG.md
   └─ PRIVACY.md
```

### 13.1 测试缝

内部实现不得依赖“无法替换的匿名 namespace 静态函数”来模拟外部世界。至少抽象：

```cpp
struct IRegistryStore;
struct IFileStore;
struct IRandomSource;
struct ISystemQueries;
struct IClock;
```

生产构建注入 Win32 实现；单元测试注入内存替身和故障脚本。接口放在 `src/internal`，不属于公共 ABI。

---

## 14. 构建与发布

### 14.1 编译语言与工具链

- 语言最低标准：C++11；推荐以 C++17 编译，但公共接口不得依赖 C++17 类型。
- Windows 发布基线：Visual Studio 2022 / MSVC v143 的固定、可复现版本。
- 禁止使用 Visual Studio 2015 作为新项目的受支持发布基线。
- Windows 7 兼容构建不得使用 Visual Studio 2026 工具集；应固定 VS2022/v143 及对应 Windows SDK，并在真实 Win7 SP1 上验证。
- MinGW-w64 可作为社区/可选构建，但不承诺与 MSVC C++ ABI 互通；C ABI 必须通过兼容测试。

### 14.2 目标宏

Windows 7 构建：

```text
_WIN32_WINNT=0x0601
WINVER=0x0601
UNICODE
_UNICODE
WIN32_LEAN_AND_MEAN
NOMINMAX
```

### 14.3 链接库

必须或按模块需要链接：

- `bcrypt.lib`：随机数。
- `iphlpapi.lib`：网络适配器和接口信息。
- `advapi32.lib`：注册表、用户名、安全信息。
- `user32.lib`：显示模式及回退指标。
- `shell32.lib`：Known Folder。
- `ole32.lib`：`CoTaskMemFree`。
- `kernel32.lib`：系统、文件、同步 API（通常隐式链接）。

V2 主实现不再要求 `rpcrt4.lib`。`ntdll.dll` 中的 `RtlGetVersion` 通过动态查找，不链接 `ntdll.lib`。

### 14.4 编译与安全选项

MSVC 推荐：

```text
/utf-8 /W4 /permissive- /EHsc /guard:cf /DYNAMICBASE /NXCOMPAT /sdl
```

- 发布构建警告视为错误，第三方/系统头警告可局部隔离。
- 禁止关闭栈保护。
- 静态库必须提供与宿主匹配的 `/MD` 和 `/MT` 变体，或明确要求由源码集成。
- DLL 的 C ABI 不跨边界转移堆内存，可选择 `/MT` 降低部署依赖；安全更新后必须重编发布。
- PDB、头文件、导入库、静态库、版本信息和许可证清单必须随发布包提供。

### 14.5 产物

```text
include/DeviceInfoSDK/*.h
lib/x86/DeviceInfoSDK-md.lib
lib/x64/DeviceInfoSDK-md.lib
lib/x86/DeviceInfoSDK-mt.lib
lib/x64/DeviceInfoSDK-mt.lib
bin/x86/DeviceInfoSDK.dll        # 可选
bin/x64/DeviceInfoSDK.dll        # 可选
bin/*/deviceinfo_cli.exe
symbols/*.pdb
```

DLL 文件版本与 `GetSdkVersion()` 必须一致，采用语义化版本 `MAJOR.MINOR.PATCH`。

---

## 15. 兼容性策略

### 15.1 支持矩阵

| 系统 | 架构 | 级别 | 要求 |
|---|---|---|---|
| Windows 11 | x64 | 主要支持 | CI + 物理机/VM 集成测试 |
| Windows 10 | x86/x64 | 兼容支持 | CI + 至少一台 22H2/企业受控镜像测试 |
| Windows 7 SP1 | x86/x64 | 兼容性目标 | 单独固定工具链；不代表系统仍获微软安全支持；发布前业务签字 |
| Windows Server | x64 | 尽力兼容 | 服务账号、无交互桌面、RDP 场景测试 |
| ARM64 | ARM64 | V2 非必须 | 后续版本可增加 |

### 15.2 Windows 7 风险声明

Windows 7 已结束微软产品支持。保留兼容性意味着：

- SDK 只能承诺 API 可运行，不承诺操作系统安全性。
- 必须维护独立构建环境、依赖版本和回归机。
- 新版工具链或运行库升级可能迫使停止 Win7 产物。
- 项目立项时必须确认 Win7 是否仍为真实业务需求；若无强制客户约束，应从正式支持范围移除。

---

## 16. 性能与资源指标

### 16.1 指标

参考环境：Windows 11 x64、4 核以上、SSD、一个活动网络适配器、Release 构建。

| 指标 | 目标 |
|---|---:|
| 热调用 P50 | ≤ 20 ms |
| 热调用 P95 | ≤ 50 ms |
| 首次生成并双写 ID 的冷调用 P95 | ≤ 150 ms |
| 单次峰值额外堆内存 | ≤ 2 MiB |
| 持久线程 | 0 |
| 10,000 次循环后 HANDLE/HKEY 净增长 | 0 |

`GetAdaptersAddresses` 是同步且可能消耗较多时间的系统调用，因此 50 ms 不是硬实时上限。测试必须记录分项耗时；调用方若对延迟敏感，可关闭 `kCollectNetworkType` 和 `kCollectMac`。

### 16.2 禁止的性能优化

- 禁止永久缓存动态网络类型、屏幕分辨率、主机名或用户名。
- 禁止为了速度跳过 ID 校验或原子写入。
- 禁止在后台偷偷预采集。
- 允许在单次调用内复用网络枚举结果和系统路径结果。

---

## 17. 测试规范

### 17.1 Device ID 单元测试

必须覆盖：

1. 两处都不存在，生成、双写、二次读取稳定。
2. 仅注册表有效，回填文件。
3. 仅文件有效，回填注册表。
4. 两处相同。
5. 两处均有效但不同，注册表胜出并修复文件。
6. 大写 UUID 归一化为小写。
7. 非 v4、错误 variant、错误连字符、附加字符、BOM、嵌入 NUL、超长内容均拒绝。
8. 注册表权限拒绝、文件权限拒绝、磁盘满、只读目录。
9. 一处写成功、一处写失败。
10. 两处写失败，返回非空 fallback 且不持久化。
11. 随机源失败。
12. 临时文件写入后进程模拟崩溃，不破坏已有目标文件。
13. mutex 超时和 abandoned mutex。
14. 50 线程并发首次调用只产生一个最终 ID。
15. 至少 20 个进程并发首次调用只产生一个最终 ID。
16. `DeletePersistedDeviceId` 双删、部分失败、删后重建。
17. V1 迁移开/关与非法旧值。

### 17.2 采集器单元测试

- UTF-16 正常、代理对、非法代理项、空字符串和超长字符串。
- OS 版本 API 缺失/失败。
- 显示主路径和 DPI 回退路径。
- SMBIOS Type 1 正常、无 Type 1、索引 0、越界索引、缺双 NUL、长度溢出和 1 MiB 上限。
- CPU 注册表类型错误、空白清洗。
- 处理器组统计失败回退。
- 内存、存储边界值与 64 位算术。
- 网络：Ethernet、Wi-Fi、双网卡、VPN、Hyper-V、loopback、tunnel、无网关、断网、IPv6-only、指标相同、随机 MAC。
- 主机名两阶段扩容。
- 敏感字段未授权时对应 Win32 API 不得被调用。

### 17.3 Fuzz / 鲁棒性测试

必须对以下解析入口做基于字节流的 fuzz：

- SMBIOS 结构表。
- UUID 文本解析。
- 注册表/文件 UTF-8 内容。
- C ABI 结构体 `struct_size`、非 NUL 终止和随机输入。

目标：无崩溃、无越界、无整数溢出、无无限循环。

### 17.4 ABI 测试

- x86/x64 检查 `sizeof`、`offsetof` 和 `#pragma pack(8)`。
- 使用 C 编译器直接包含 `DeviceInfoC.h`。
- C# P/Invoke、Rust FFI、Python `ctypes` 调用成功。
- 较小但合法的旧 `struct_size` 不越界写。
- 非法 ABI 版本返回 `kInvalidArgument`。
- DLL 与静态库输出一致。

### 17.5 集成测试矩阵

- Windows 11 x64 物理机和 VM。
- Windows 10 x86/x64。
- Windows 7 SP1 x86/x64（仅在产品继续承诺时）。
- 普通用户、管理员、中文用户名、包含非 BMP 字符的用户名。
- 域账号、漫游配置环境、RDP、服务账号。
- 单屏、多屏、高 DPI、改变分辨率后再次调用。
- 有线、Wi-Fi、同时连接、VPN、Hyper-V/VMware 虚拟网卡。
- FAT/NTFS（如支持）、LocalAppData 不可写、注册表不可写。

### 17.6 性能与泄漏测试

- 预热 5 次，测量 100 次，输出 P50/P95/P99。
- 10,000 次循环检查 Private Bytes、HANDLE、GDI/USER 对象、HKEY。
- 使用 Application Verifier、AddressSanitizer（可用构建）或等价工具执行异常路径。

---

## 18. 命令行验证工具

测试工具不属于 SDK ABI，但必须随源码和二进制提供。

### 18.1 参数

```text
deviceinfo_cli --namespace <id> --app-version <ver> [options]

--format text|json        默认 text
-e json                   兼容别名，等价于 --format json
--app-channel <channel>
--include-sensitive       同时启用 mac/hostname/os_username
--no-network              关闭 network_type 和 mac
--repeat <N>              连续采集 N 次，用于稳定性测试
--delete-device-id        显式删除当前 namespace 的持久 ID 后退出
--show-status             text 模式显示 masks/diagnostic/native_error
```

### 18.2 输出规则

- text 模式：每行 `key=value`，随后可显示状态。
- JSON 模式：stdout **只能**包含一个合法 JSON 对象；不得先输出键值对。
- 诊断说明写 stderr，但不得包含敏感字段值。
- JSON 必须正确转义引号、反斜杠、控制字符和 UTF-8。
- CLI 的 JSON 代码不得链接回 SDK 核心库形成序列化依赖。

示例：

```json
{
  "platform": "windows",
  "device_id": "550e8400-e29b-41d4-a716-446655440000",
  "app_version": "2.5.0",
  "app_channel": "steam",
  "os": "Windows",
  "os_version": "10.0.22631",
  "screen_width": 2560,
  "screen_height": 1440,
  "model": "Example Model",
  "brand": "Example Vendor",
  "mac": "",
  "cpuid": "",
  "cpu_model": "Example CPU",
  "cpu_cores": 16,
  "memory_gb": 31,
  "storage_gb": 953,
  "network_type": "ethernet",
  "lang": "zh-CN",
  "hostname": "",
  "os_username": "",
  "_status": {
    "result_code": 0,
    "present_mask": "0x000000000003f3ff",
    "error_mask": "0x0000000000000000",
    "diagnostic_flags": "0x0000000000000000",
    "native_error": 0
  }
}
```

退出码：

| Exit Code | 含义 |
|---:|---|
| 0 | `kOk` |
| 1 | `kPartial` |
| 2 | 参数错误 |
| 3 | `kDeviceIdVolatile` 或 `kBusy` |
| 4 | `kInternalError` |

---

## 19. 验收标准（Definition of Done）

开发交付必须同时满足：

1. C++ API 和 C ABI 均实现并有示例。
2. `device_id` 通过格式、版本、variant、并发、冲突、崩溃恢复和权限失败测试。
3. 同一用户/本机/namespace 重启 100 次 ID 不变。
4. 20 进程并发首次启动最终持久 ID 唯一且一致。
5. 敏感采集默认关闭，未开启时不调用对应 API。
6. 网络适配器选择可复现，Ethernet/Wi-Fi 取值符合规则。
7. SMBIOS 解析无越界并通过 fuzz 语料。
8. 所有公开字符串为有效 UTF-8。
9. JSON 模式 stdout 可被标准 JSON 解析器直接解析。
10. x86/x64 ABI 布局测试通过。
11. 10,000 次调用无句柄净泄漏。
12. 达到第 16 节性能目标，或提交有数据的例外评审。
13. `/W4` 发布构建无未处理警告，静态分析无 P0/P1 缺陷。
14. 发布包含头文件、库、DLL（如提供）、PDB、版本说明、隐私说明、测试报告。
15. Windows 7 若仍在范围内，必须由产品/安全负责人接受其生命周期风险，并在真实 Win7 SP1 x86/x64 上通过回归。

---

## 20. 待项目方确认的配置项

以下内容必须在编码冻结前填写，不得保留示例值：

| 项目 | 当前 V2 默认/建议 | 必须确认 |
|---|---|---|
| 生产 `id_namespace` | 无默认；示例 `com.example.product` 会被拒绝 | 是 |
| Windows 7 是否正式交付 | 兼容性目标、默认需业务签字 | 是 |
| 是否需要 DLL | 建议同时提供静态库与 C ABI DLL | 是 |
| 敏感字段业务开关 | 默认全部关闭 | 是 |
| V1 ID 迁移 | 新项目默认关闭 | 是 |
| 上报字段是否包含 `_status` | SDK 提供；业务协议自行决定 | 是 |
| Win10/Win11 最低版本与测试镜像 | 由发布环境确定 | 是 |

---

## 21. 风险清单

| 风险 | 可能性 | 影响 | 应对 |
|---|---:|---:|---|
| 用户手工篡改 ID | 中 | 中 | 明确非安全信任根；服务端不得单因该字段授权 |
| 双存储短暂不一致 | 中 | 低 | 每次读取校验并按矩阵修复；原子文件替换 |
| VPN/虚拟网卡被选中 | 中 | 中 | `GetIfEntry2` 硬件标志、网关和指标排序；输出诊断标记；文档声明 best effort |
| 企业策略禁止读 SMBIOS/注册表/LocalAppData | 低～中 | 中 | 字段级错误状态和降级，不崩溃 |
| 网络枚举延迟超过 50 ms | 中 | 低 | 分项性能监控；允许关闭网络采集 |
| Windows 7 工具链冻结 | 高 | 高 | 独立构建链和回归机；评估移除 Win7 |
| C++ ABI 不兼容 | 中 | 高 | DLL/FFI 强制使用 C ABI；不跨边界分配内存 |
| VDI/快照导致 ID 克隆或回滚 | 中 | 中 | 文档明确边界；服务端结合账号、会话和时间信号处理 |
| LocalAppData 或用户配置被清理 | 中 | 中 | 双存储修复；两处都被清理时生成新 ID |
| 上层过度收集敏感字段 | 中 | 高 | 默认关闭；独立隐私文档；显式参数和代码审查 |

---

## 22. 版本与兼容策略

- SDK 版本采用 `MAJOR.MINOR.PATCH`。
- C ABI 结构体只允许尾部追加字段；已有字段偏移不得改变。
- 破坏 C ABI、字段语义、ID namespace 或持久化主规则时必须提升 MAJOR。
- 修复采集准确性但不改变字段语义可提升 PATCH/MINOR，并在 CHANGELOG 中说明可能的数据变化。
- 持久化路径与 UUID 校验规则在 2.x 内保持稳定。
- 不得在无迁移方案时更换 namespace 或删除旧存储。

---

## 23. 官方技术参考

1. RFC Editor, **RFC 9562 — Universally Unique IDentifiers (UUIDs)**：<https://www.rfc-editor.org/rfc/rfc9562.html>
2. Microsoft Learn, **BCryptGenRandom**：<https://learn.microsoft.com/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom>
3. Microsoft Learn, **GetAdaptersAddresses**：<https://learn.microsoft.com/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersaddresses>
4. Microsoft Learn, **IP_ADAPTER_ADDRESSES**：<https://learn.microsoft.com/windows/win32/api/iptypes/ns-iptypes-ip_adapter_addresses_lh>
5. Microsoft Learn, **GetIfEntry2 / MIB_IF_ROW2**：<https://learn.microsoft.com/windows/win32/api/netioapi/nf-netioapi-getifentry2>、<https://learn.microsoft.com/windows/win32/api/netioapi/ns-netioapi-mib_if_row2>
6. Microsoft Learn, **GetSystemFirmwareTable**：<https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemfirmwaretable>
7. DMTF, **SMBIOS Specification 3.9.0 (DSP0134)**：<https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.9.0.pdf>
8. Microsoft Learn, **EnumDisplaySettings**：<https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-enumdisplaysettingsw>
9. Microsoft Learn, **RtlGetVersion**：<https://learn.microsoft.com/windows/win32/devnotes/rtlgetversion>
10. Microsoft Learn, **GetActiveProcessorCount**：<https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-getactiveprocessorcount>
11. Microsoft Learn, **SHGetKnownFolderPath**：<https://learn.microsoft.com/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath>
12. Microsoft Learn, **MoveFileExW**：<https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-movefileexw>
13. Microsoft Learn, **Predefined Registry Keys / HKEY_CURRENT_USER_LOCAL_SETTINGS**：<https://learn.microsoft.com/windows/win32/sysinfo/predefined-keys>
14. Microsoft Learn, **Windows 7 Lifecycle**：<https://learn.microsoft.com/lifecycle/products/windows-7>
15. Microsoft Learn, **Supported Platforms (Microsoft C++)**：<https://learn.microsoft.com/cpp/overview/supported-platforms-visual-cpp>

---

## 24. 变更记录

| 版本 | 日期 | 变更 |
|---|---|---|
| 1.0 | 2026-06-26 | 初始版本 |
| 2.0 | 2026-06-26 | 完成 ABI、ID 语义、并发持久化、网络选择、SMBIOS、隐私、诊断、构建和验收体系补全 |

---

**文档结束**
