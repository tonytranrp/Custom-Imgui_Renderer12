#pragma once
#include <cstddef>
#include <cstdint>

enum class ImageFetchStatus : int32_t {
    Loading = 0,
    Ready = 1,
    Failed = 2,
    InvalidId = 3
};

enum class ImageSourceKind : int32_t {
    Url = 0,
    LocalPath = 1
};

enum class FetchedMediaKind : int32_t {
    StaticRGBA = 0,
    AnimatedRGBA = 1
};

struct AnimatedFrameFFI {
    uint8_t* data;
    size_t len;
    int width;
    int height;
    uint32_t delay_ms;
};

extern "C" {
    // Fetches data from URL (blocking)
    // Returns pointer to heap allocated bytes, writes length to out_len
    // Returns nullptr on failure
    uint8_t* fetch_url_data(const char* url, size_t* out_len);

    // Frees the bytes returned by fetch_url_data
    void free_rust_bytes(uint8_t* ptr, size_t len);

    // Starts fetching an image from a URL asynchronously
    // Returns a unique ID for the fetch operation
    uint64_t start_fetch_image(const char* url);

    // Starts fetching media from either URL or local path.
    uint64_t start_fetch_media(const char* source, int32_t source_kind);

    // Cancels and removes a pending fetch request from the shared map.
    // Safe to call for completed/invalid IDs (no-op).
    void cancel_fetch_request(uint64_t id);

    // Checks the status of an image fetch operation
    // If the image is ready, returns a pointer to the heap-allocated image data
    // and writes its length, width, and height to the output parameters.
    // Returns nullptr if the image is not ready or if the fetch failed.
    uint8_t* check_fetch_status(uint64_t id, size_t* out_len, int* out_width, int* out_height);

    // Extended status API:
    // - Returns one of ImageFetchStatus values as int32_t.
    // - On Ready, out_data/out_len/out_width/out_height are filled.
    // - On Loading/Failed/InvalidId, out_data is set to nullptr.
    int32_t check_fetch_status_ex(
        uint64_t id,
        uint8_t** out_data,
        size_t* out_len,
        int* out_width,
        int* out_height
    );

    // Media status API with animated frame support.
    int32_t check_fetch_media_status_ex(
        uint64_t id,
        int32_t* out_media_kind,
        uint8_t** out_data,
        size_t* out_len,
        int* out_width,
        int* out_height,
        AnimatedFrameFFI** out_frames,
        size_t* out_frame_count
    );

    // Frees the memory allocated for the image data returned by check_fetch_status
    void free_image_data(uint8_t* ptr, size_t len);

    // Frees animated frames returned by check_fetch_media_status_ex.
    void free_animation_frames(AnimatedFrameFFI* frames, size_t frame_count);
}
