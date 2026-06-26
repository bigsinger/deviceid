# DeviceInfoSDK Changelog

## 2.0.0 - 2026-06-26

- Added the V2 C++ API and fixed-layout C ABI.
- Added UUIDv4 device ID generation with registry and LocalAppData persistence.
- Added default privacy behavior: MAC, hostname, and username are opt-in only.
- Added Win32 collectors for OS version, display mode, SMBIOS brand/model, CPU, memory, storage, network type, locale, hostname, and username.
- Added `deviceinfo_cli` with text/JSON output, repeat mode, deletion, status output, and local smoke tests.

## Verification Notes

- This source tree includes local smoke and ABI tests.
- Full release acceptance still requires the SPEC matrix: x86/x64 release artifacts, multi-process concurrency stress, leak/performance runs, and any committed Windows 7 SP1 regression machines.
