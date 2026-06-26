# DeviceInfoSDK Privacy Notes

DeviceInfoSDK does not make network requests, upload telemetry, or write logs.

The default collection mode excludes high-sensitivity fields:

- `mac`
- `hostname`
- `os_username`

Those fields are collected only when the caller explicitly sets `kCollectMac`, `kCollectHostname`, or `kCollectUsername`.

`device_id` is a pseudonymous installation identifier scoped to the current Windows user, current machine, and configured namespace. It is not a hardware identity, authentication secret, license proof, encryption key, or fraud decision by itself.

Persisted ID locations:

- `HKEY_CURRENT_USER_LOCAL_SETTINGS\Software\DeviceInfoSDK\Namespaces\<id_namespace>\DeviceId`
- `%LOCALAPPDATA%\DeviceInfoSDK\<id_namespace>\device_id`

Callers that collect or transmit the output should apply their own notice, consent, retention, access control, deletion, and transport protection requirements.
