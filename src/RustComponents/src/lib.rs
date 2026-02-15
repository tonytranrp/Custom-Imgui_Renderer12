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

// Fetch URL Data
#[no_mangle]
pub extern "C" fn fetch_url_data(url: *const c_char, out_len: *mut usize) -> *mut u8 {
    if url.is_null() { return std::ptr::null_mut(); }
    
    let c_str = unsafe { std::ffi::CStr::from_ptr(url) };
    let url_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };

    match ureq::get(url_str).call() {
        Ok(response) => {
            let mut reader = response.into_reader();
            let mut bytes = Vec::new();
            if std::io::copy(&mut reader, &mut bytes).is_ok() {
                unsafe {
                    if !out_len.is_null() {
                        *out_len = bytes.len();
                    }
                }
                
                // Convert to Box<[u8]> to ensure capacity == length
                let mut boxed_slice = bytes.into_boxed_slice();
                let ptr = boxed_slice.as_mut_ptr(); // Get pointer to data
                std::mem::forget(boxed_slice);      // Leak the box
                
                return ptr;
            }
        },
        Err(_) => {}
    }
    
    std::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn free_rust_bytes(ptr: *mut u8, len: usize) {
    if ptr.is_null() { return; }
    unsafe {
        // Reconstruct the fat pointer for the slice
        let slice_ptr = std::ptr::slice_from_raw_parts_mut(ptr, len);
        // Reconstruct the Box<[u8]> and drop it
        let _ = Box::from_raw(slice_ptr);
    }
}
