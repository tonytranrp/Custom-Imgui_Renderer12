#![deny(unused_must_use)]
#![deny(warnings)]

use std::collections::HashMap;
use std::ffi::CStr;
use std::io::{Cursor, Read};
use std::os::raw::c_char;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use image::codecs::gif::GifDecoder;
use image::{AnimationDecoder, ImageFormat};
use lazy_static::lazy_static;

#[repr(i32)]
enum FetchStatusCode {
    Loading = 0,
    Ready = 1,
    Failed = 2,
    InvalidId = 3,
}

#[repr(i32)]
enum SourceKind {
    Url = 0,
    LocalPath = 1,
}

#[repr(i32)]
enum MediaKind {
    StaticRgba = 0,
    AnimatedRgba = 1,
}

#[repr(C)]
pub struct AnimatedFrameFFI {
    data: *mut u8,
    len: usize,
    width: i32,
    height: i32,
    delay_ms: u32,
}

struct OwnedFrame {
    data: Vec<u8>,
    width: i32,
    height: i32,
    delay_ms: u32,
}

enum FetchMediaResult {
    Static {
        data: Vec<u8>,
        width: i32,
        height: i32,
    },
    Animated {
        frames: Vec<OwnedFrame>,
    },
}

enum FetchState {
    Loading,
    Ready(FetchMediaResult),
    Failed,
}

enum FetchBytesState {
    Loading,
    Ready(Vec<u8>),
    Failed,
}

lazy_static! {
    static ref PENDING_REQUESTS: Mutex<HashMap<u64, Arc<Mutex<FetchState>>>> =
        Mutex::new(HashMap::new());
    static ref PENDING_BYTE_REQUESTS: Mutex<HashMap<u64, Arc<Mutex<FetchBytesState>>>> =
        Mutex::new(HashMap::new());
}

static NEXT_ID: AtomicU64 = AtomicU64::new(1);

fn normalize_delay_ms(numer: u32, denom: u32) -> u32 {
    if numer == 0 {
        return 100;
    }
    if denom == 0 {
        return numer.max(1);
    }
    let ms = ((numer as f32) / (denom as f32)).round() as u32;
    ms.max(1)
}

fn load_source_bytes(source: &str, source_kind: i32) -> Result<Vec<u8>, String> {
    if source.is_empty() {
        return Err("empty source".to_string());
    }

    if source_kind == SourceKind::Url as i32 {
        let agent = ureq::AgentBuilder::new()
            .timeout_connect(Duration::from_secs(5))
            .timeout_read(Duration::from_secs(20))
            .timeout_write(Duration::from_secs(20))
            .build();

        let response = agent
            .get(source)
            .call()
            .map_err(|e| format!("network error: {e}"))?;
        let mut reader = response.into_reader();
        let mut bytes = Vec::new();
        reader
            .read_to_end(&mut bytes)
            .map_err(|e| format!("read error: {e}"))?;
        return Ok(bytes);
    }

    if source_kind == SourceKind::LocalPath as i32 {
        return std::fs::read(source).map_err(|e| format!("file read error: {e}"));
    }

    Err("invalid source kind".to_string())
}

#[no_mangle]
pub extern "C" fn cancel_fetch_request(id: u64) {
    if id == 0 {
        return;
    }
    PENDING_REQUESTS.lock().unwrap().remove(&id);
    PENDING_BYTE_REQUESTS.lock().unwrap().remove(&id);
}

fn decode_media(bytes: &[u8]) -> Result<FetchMediaResult, String> {
    if bytes.is_empty() {
        return Err("empty payload".to_string());
    }

    let guessed_format = image::guess_format(bytes).ok();
    if matches!(guessed_format, Some(ImageFormat::Gif)) {
        let decoder =
            GifDecoder::new(Cursor::new(bytes)).map_err(|e| format!("gif decoder error: {e}"))?;
        let frames = decoder
            .into_frames()
            .collect_frames()
            .map_err(|e| format!("gif frames error: {e}"))?;

        if !frames.is_empty() {
            let mut out_frames = Vec::with_capacity(frames.len());
            for frame in frames {
                let (numer, denom) = frame.delay().numer_denom_ms();
                let delay_ms = normalize_delay_ms(numer, denom);
                let buffer = frame.into_buffer();
                let (width, height) = buffer.dimensions();
                out_frames.push(OwnedFrame {
                    data: buffer.into_raw(),
                    width: width as i32,
                    height: height as i32,
                    delay_ms,
                });
            }

            if out_frames.len() > 1 {
                return Ok(FetchMediaResult::Animated { frames: out_frames });
            }

            if let Some(frame) = out_frames.pop() {
                return Ok(FetchMediaResult::Static {
                    data: frame.data,
                    width: frame.width,
                    height: frame.height,
                });
            }
        }
    }

    let image = image::load_from_memory(bytes).map_err(|e| format!("decode error: {e}"))?;
    let rgba = image.to_rgba8();
    let (width, height) = rgba.dimensions();
    Ok(FetchMediaResult::Static {
        data: rgba.into_raw(),
        width: width as i32,
        height: height as i32,
    })
}

// Fetch URL Data
#[no_mangle]
/// # Safety
/// `url` must be a valid null-terminated C string pointer. `out_len` may be null;
/// if non-null it must point to writable memory for one `usize`.
pub unsafe extern "C" fn fetch_url_data(url: *const c_char, out_len: *mut usize) -> *mut u8 {
    if url.is_null() {
        return std::ptr::null_mut();
    }

    let c_str = CStr::from_ptr(url);
    let url_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };

    match ureq::get(url_str).call() {
        Ok(response) => {
            let mut reader = response.into_reader();
            let mut bytes = Vec::new();
            if std::io::copy(&mut reader, &mut bytes).is_ok() {
                if !out_len.is_null() {
                    *out_len = bytes.len();
                }

                let mut boxed_slice = bytes.into_boxed_slice();
                let ptr = boxed_slice.as_mut_ptr();
                std::mem::forget(boxed_slice);
                ptr
            } else {
                std::ptr::null_mut()
            }
        }
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
/// # Safety
/// `ptr`/`len` must be returned from `fetch_url_data` in this crate and not
/// already freed.
pub unsafe extern "C" fn free_rust_bytes(ptr: *mut u8, len: usize) {
    if ptr.is_null() {
        return;
    }
    let slice_ptr = std::ptr::slice_from_raw_parts_mut(ptr, len);
    let _ = Box::from_raw(slice_ptr);
}

#[no_mangle]
/// # Safety
/// `source` must be a valid null-terminated C string pointer.
pub unsafe extern "C" fn start_fetch_media(source: *const c_char, source_kind: i32) -> u64 {
    if source.is_null() {
        return 0;
    }

    let c_str = CStr::from_ptr(source);
    let source_str = match c_str.to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return 0,
    };

    let id = NEXT_ID.fetch_add(1, Ordering::SeqCst);
    let state = Arc::new(Mutex::new(FetchState::Loading));
    PENDING_REQUESTS.lock().unwrap().insert(id, state.clone());

    thread::spawn(move || {
        let result =
            load_source_bytes(&source_str, source_kind).and_then(|bytes| decode_media(&bytes));

        let mut state_guard = state.lock().unwrap();
        *state_guard = match result {
            Ok(payload) => FetchState::Ready(payload),
            Err(_) => FetchState::Failed,
        };
    });

    id
}

#[no_mangle]
/// # Safety
/// `source` must be a valid null-terminated C string pointer.
pub unsafe extern "C" fn start_fetch_bytes(source: *const c_char, source_kind: i32) -> u64 {
    if source.is_null() {
        return 0;
    }

    let c_str = CStr::from_ptr(source);
    let source_str = match c_str.to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return 0,
    };

    let id = NEXT_ID.fetch_add(1, Ordering::SeqCst);
    let state = Arc::new(Mutex::new(FetchBytesState::Loading));
    PENDING_BYTE_REQUESTS
        .lock()
        .unwrap()
        .insert(id, state.clone());

    thread::spawn(move || {
        let result = load_source_bytes(&source_str, source_kind);
        let mut state_guard = state.lock().unwrap();
        *state_guard = match result {
            Ok(bytes) => FetchBytesState::Ready(bytes),
            Err(_) => FetchBytesState::Failed,
        };
    });

    id
}

#[no_mangle]
/// # Safety
/// `url` must be a valid null-terminated C string pointer.
pub unsafe extern "C" fn start_fetch_image(url: *const c_char) -> u64 {
    start_fetch_media(url, SourceKind::Url as i32)
}

#[no_mangle]
/// # Safety
/// All output pointers are optional but, when non-null, must point to writable
/// memory for their corresponding types.
pub unsafe extern "C" fn check_fetch_media_status_ex(
    id: u64,
    out_media_kind: *mut i32,
    out_data: *mut *mut u8,
    out_len: *mut usize,
    out_width: *mut i32,
    out_height: *mut i32,
    out_frames: *mut *mut AnimatedFrameFFI,
    out_frame_count: *mut usize,
) -> i32 {
    if !out_media_kind.is_null() {
        *out_media_kind = MediaKind::StaticRgba as i32;
    }
    if !out_data.is_null() {
        *out_data = std::ptr::null_mut();
    }
    if !out_len.is_null() {
        *out_len = 0;
    }
    if !out_width.is_null() {
        *out_width = 0;
    }
    if !out_height.is_null() {
        *out_height = 0;
    }
    if !out_frames.is_null() {
        *out_frames = std::ptr::null_mut();
    }
    if !out_frame_count.is_null() {
        *out_frame_count = 0;
    }

    let state_arc = {
        let requests = PENDING_REQUESTS.lock().unwrap();
        if let Some(arc) = requests.get(&id) {
            arc.clone()
        } else {
            return FetchStatusCode::InvalidId as i32;
        }
    };

    let mut state_guard = state_arc.lock().unwrap();
    match std::mem::replace(&mut *state_guard, FetchState::Loading) {
        FetchState::Loading => {
            *state_guard = FetchState::Loading;
            FetchStatusCode::Loading as i32
        }
        FetchState::Failed => {
            drop(state_guard);
            PENDING_REQUESTS.lock().unwrap().remove(&id);
            FetchStatusCode::Failed as i32
        }
        FetchState::Ready(payload) => {
            drop(state_guard);
            PENDING_REQUESTS.lock().unwrap().remove(&id);

            match payload {
                FetchMediaResult::Static {
                    data,
                    width,
                    height,
                } => {
                    let mut boxed_slice = data.into_boxed_slice();
                    let len = boxed_slice.len();
                    let ptr = boxed_slice.as_mut_ptr();
                    std::mem::forget(boxed_slice);

                    if !out_media_kind.is_null() {
                        *out_media_kind = MediaKind::StaticRgba as i32;
                    }
                    if !out_data.is_null() {
                        *out_data = ptr;
                    }
                    if !out_len.is_null() {
                        *out_len = len;
                    }
                    if !out_width.is_null() {
                        *out_width = width;
                    }
                    if !out_height.is_null() {
                        *out_height = height;
                    }

                    FetchStatusCode::Ready as i32
                }
                FetchMediaResult::Animated { frames } => {
                    if frames.is_empty() {
                        return FetchStatusCode::Failed as i32;
                    }

                    let mut ffi_frames = Vec::with_capacity(frames.len());
                    for frame in frames {
                        let mut boxed_slice = frame.data.into_boxed_slice();
                        let ptr = boxed_slice.as_mut_ptr();
                        let len = boxed_slice.len();
                        std::mem::forget(boxed_slice);

                        ffi_frames.push(AnimatedFrameFFI {
                            data: ptr,
                            len,
                            width: frame.width,
                            height: frame.height,
                            delay_ms: frame.delay_ms,
                        });
                    }

                    let mut boxed_frames = ffi_frames.into_boxed_slice();
                    let frame_count = boxed_frames.len();
                    let frame_ptr = boxed_frames.as_mut_ptr();
                    std::mem::forget(boxed_frames);

                    if !out_media_kind.is_null() {
                        *out_media_kind = MediaKind::AnimatedRgba as i32;
                    }
                    if !out_frames.is_null() {
                        *out_frames = frame_ptr;
                    }
                    if !out_frame_count.is_null() {
                        *out_frame_count = frame_count;
                    }

                    FetchStatusCode::Ready as i32
                }
            }
        }
    }
}

#[no_mangle]
/// # Safety
/// All output pointers are optional but, when non-null, must point to writable
/// memory for their corresponding types.
pub unsafe extern "C" fn check_fetch_bytes_status_ex(
    id: u64,
    out_data: *mut *mut u8,
    out_len: *mut usize,
) -> i32 {
    if !out_data.is_null() {
        *out_data = std::ptr::null_mut();
    }
    if !out_len.is_null() {
        *out_len = 0;
    }

    let state_arc = {
        let requests = PENDING_BYTE_REQUESTS.lock().unwrap();
        if let Some(arc) = requests.get(&id) {
            arc.clone()
        } else {
            return FetchStatusCode::InvalidId as i32;
        }
    };

    let mut state_guard = state_arc.lock().unwrap();
    match std::mem::replace(&mut *state_guard, FetchBytesState::Loading) {
        FetchBytesState::Loading => {
            *state_guard = FetchBytesState::Loading;
            FetchStatusCode::Loading as i32
        }
        FetchBytesState::Failed => {
            drop(state_guard);
            PENDING_BYTE_REQUESTS.lock().unwrap().remove(&id);
            FetchStatusCode::Failed as i32
        }
        FetchBytesState::Ready(bytes) => {
            drop(state_guard);
            PENDING_BYTE_REQUESTS.lock().unwrap().remove(&id);

            let mut boxed_slice = bytes.into_boxed_slice();
            let len = boxed_slice.len();
            let ptr = boxed_slice.as_mut_ptr();
            std::mem::forget(boxed_slice);

            if !out_data.is_null() {
                *out_data = ptr;
            }
            if !out_len.is_null() {
                *out_len = len;
            }

            FetchStatusCode::Ready as i32
        }
    }
}

#[no_mangle]
/// # Safety
/// All output pointers are optional but, when non-null, must point to writable
/// memory for their corresponding types.
pub unsafe extern "C" fn check_fetch_status_ex(
    id: u64,
    out_data: *mut *mut u8,
    out_len: *mut usize,
    out_width: *mut i32,
    out_height: *mut i32,
) -> i32 {
    let mut media_kind = MediaKind::StaticRgba as i32;
    let mut static_data: *mut u8 = std::ptr::null_mut();
    let mut static_len: usize = 0;
    let mut static_width: i32 = 0;
    let mut static_height: i32 = 0;
    let mut frames_ptr: *mut AnimatedFrameFFI = std::ptr::null_mut();
    let mut frame_count: usize = 0;

    let status = check_fetch_media_status_ex(
        id,
        &mut media_kind,
        &mut static_data,
        &mut static_len,
        &mut static_width,
        &mut static_height,
        &mut frames_ptr,
        &mut frame_count,
    );

    if status != FetchStatusCode::Ready as i32 {
        if !out_data.is_null() {
            *out_data = std::ptr::null_mut();
        }
        return status;
    }

    if media_kind == MediaKind::StaticRgba as i32 {
        if !out_data.is_null() {
            *out_data = static_data;
        }
        if !out_len.is_null() {
            *out_len = static_len;
        }
        if !out_width.is_null() {
            *out_width = static_width;
        }
        if !out_height.is_null() {
            *out_height = static_height;
        }
        return FetchStatusCode::Ready as i32;
    }

    if frames_ptr.is_null() || frame_count == 0 {
        return FetchStatusCode::Failed as i32;
    }

    let boxed_frames =
        Box::from_raw(std::ptr::slice_from_raw_parts_mut(frames_ptr, frame_count));
    let mut frames = boxed_frames.into_vec();
    if frames.is_empty() {
        return FetchStatusCode::Failed as i32;
    }

    let first = frames.remove(0);
    for frame in frames {
        if !frame.data.is_null() && frame.len > 0 {
            let _ = Vec::from_raw_parts(frame.data, frame.len, frame.len);
        }
    }

    if !out_data.is_null() {
        *out_data = first.data;
    }
    if !out_len.is_null() {
        *out_len = first.len;
    }
    if !out_width.is_null() {
        *out_width = first.width;
    }
    if !out_height.is_null() {
        *out_height = first.height;
    }

    FetchStatusCode::Ready as i32
}

#[no_mangle]
/// # Safety
/// All output pointers are optional but, when non-null, must point to writable
/// memory for their corresponding types.
pub unsafe extern "C" fn check_fetch_status(
    id: u64,
    out_len: *mut usize,
    out_width: *mut i32,
    out_height: *mut i32,
) -> *mut u8 {
    let mut out_data: *mut u8 = std::ptr::null_mut();
    let status = check_fetch_status_ex(id, &mut out_data, out_len, out_width, out_height);
    if status == FetchStatusCode::Ready as i32 {
        out_data
    } else {
        std::ptr::null_mut()
    }
}

#[no_mangle]
/// # Safety
/// `ptr`/`len` must be returned from this crate's image fetch APIs and not
/// already freed.
pub unsafe extern "C" fn free_image_data(ptr: *mut u8, len: usize) {
    if ptr.is_null() {
        return;
    }
    let _ = Vec::from_raw_parts(ptr, len, len);
}

#[no_mangle]
/// # Safety
/// `frames`/`frame_count` must be returned from `check_fetch_media_status_ex`
/// and not already freed.
pub unsafe extern "C" fn free_animation_frames(frames: *mut AnimatedFrameFFI, frame_count: usize) {
    if frames.is_null() || frame_count == 0 {
        return;
    }

    let slice = std::slice::from_raw_parts_mut(frames, frame_count);
    for frame in slice.iter_mut() {
        if !frame.data.is_null() && frame.len > 0 {
            let _ = Vec::from_raw_parts(frame.data, frame.len, frame.len);
            frame.data = std::ptr::null_mut();
            frame.len = 0;
        }
    }

    let _ = Box::from_raw(std::ptr::slice_from_raw_parts_mut(frames, frame_count));
}
