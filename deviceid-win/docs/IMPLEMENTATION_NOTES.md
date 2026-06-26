# DeviceInfoSDK Implementation Notes

This implementation targets SPEC V2 and builds `deviceId-win.exe` as the `deviceinfo_cli` validation tool while exposing the SDK headers and C ABI from `include/DeviceInfoSDK`.

Implemented locally:

- C++ API: `DeviceInfoSDK::GetDeviceInfo`, `DeletePersistedDeviceId`, `GetSdkVersion`.
- C ABI: `DISDK_GetDeviceInfoV2`, `DISDK_DeletePersistedDeviceIdV2`, `DISDK_GetSdkVersion`.
- UUIDv4 generation with `BCryptGenRandom`, strict validation, lower-case normalization, registry/file double persistence, namespace locking, conflict repair, fallback IDs, and explicit deletion.
- Default privacy behavior: MAC, hostname, and username are not collected unless requested.
- Win32 collectors for OS version, display, SMBIOS, CPU, memory, storage, network type, locale, hostname, and username.
- CLI text/JSON output, repeat mode, ID deletion, sensitive opt-in, network opt-out, status display, and `--self-test`.
- Minimal C++, C, C#, Rust, and Python integration examples.

Local verification performed:

```text
D:\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe deviceId-win.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:minimal
D:\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe deviceId-win.vcxproj /p:Configuration=Release /p:Platform=Win32 /m /v:minimal
D:\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.exe deviceId-win.vcxproj /Build "Release|x64"
deviceId-win.exe --self-test
deviceId-win.exe --namespace com.bigsinger.deviceid.dev --app-version 2.0.0 --format json --no-network | python -X utf8 -m json.tool
```

Known environment notes:

- The installed Windows SDK headers in this environment do not expose `MIB_IF_ROW2/GetIfEntry2`; the network collector uses `GetIfEntry` as a compatibility fallback and sets `kDiagVirtualAdapterPossible` when hardware certainty is limited.
- On the local machine used for verification, `GetSystemFirmwareTable('RSMB')` returned a native error, so `brand` and `model` were empty and the aggregate result was `kPartial`.
- Full Definition of Done still requires CI/package work outside this single-machine pass: release static/DLL artifacts, x86/x64 ABI automation, multi-process concurrency stress, 10,000-call leak/performance tests, fuzz runs, and any committed Windows 7 SP1 regression.
