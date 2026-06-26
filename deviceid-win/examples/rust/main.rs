#[repr(C)]
#[derive(Clone, Copy)]
struct DISDK_OptionsV2 {
    struct_size: u32,
    abi_version: u32,
    collection_flags: u64,
    lock_timeout_ms: u32,
    option_flags: u32,
    id_namespace: [u8; 65],
    app_version: [u8; 64],
    app_channel: [u8; 64],
}

const DISDK_ABI_VERSION: u32 = 2;
const DISDK_COLLECT_DEFAULT: u64 = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3);

fn main() {
    let mut options = DISDK_OptionsV2 {
        struct_size: core::mem::size_of::<DISDK_OptionsV2>() as u32,
        abi_version: DISDK_ABI_VERSION,
        collection_flags: DISDK_COLLECT_DEFAULT,
        lock_timeout_ms: 2000,
        option_flags: 0,
        id_namespace: [0; 65],
        app_version: [0; 64],
        app_channel: [0; 64],
    };
    options.id_namespace[..19].copy_from_slice(b"com.company.product");
    options.app_version[..5].copy_from_slice(b"1.0.0");
    println!("options_size={}", options.struct_size);
}
