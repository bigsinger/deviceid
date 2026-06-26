import ctypes


DISDK_ABI_VERSION = 2
DISDK_COLLECT_DEFAULT = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3)


class Options(ctypes.Structure):
    _pack_ = 8
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("abi_version", ctypes.c_uint32),
        ("collection_flags", ctypes.c_uint64),
        ("lock_timeout_ms", ctypes.c_uint32),
        ("option_flags", ctypes.c_uint32),
        ("id_namespace", ctypes.c_char * 65),
        ("app_version", ctypes.c_char * 64),
        ("app_channel", ctypes.c_char * 64),
    ]


options = Options()
options.struct_size = ctypes.sizeof(Options)
options.abi_version = DISDK_ABI_VERSION
options.collection_flags = DISDK_COLLECT_DEFAULT
options.lock_timeout_ms = 2000
options.id_namespace = b"com.company.product"
options.app_version = b"1.0.0"
print("options_size", ctypes.sizeof(Options))
