use std::ffi::CString;
use std::os::raw::c_char;

// Embed a file as a byte array
const EMBEDDED_DATA: &[u8] = include_bytes!("../assets/rust_data.txt");

#[no_mangle]
pub extern "C" fn get_embedded_data(length: *mut usize) -> *const u8 {
    unsafe {
        if !length.is_null() {
            *length = EMBEDDED_DATA.len();
        }
    }
    EMBEDDED_DATA.as_ptr()
}

#[no_mangle]
pub extern "C" fn get_rust_greeting() -> *mut c_char {
    let s = CString::new("Hello from Rust! (via Corrosion)").unwrap();
    s.into_raw()
}

#[no_mangle]
pub extern "C" fn free_rust_string(s: *mut c_char) {
    if s.is_null() { return; }
    unsafe {
        let _ = CString::from_raw(s);
    }
}
