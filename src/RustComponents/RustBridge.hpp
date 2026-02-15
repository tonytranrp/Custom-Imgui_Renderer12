#pragma once
#include <cstddef>
#include <cstdint>

extern "C" {
    // Returns a pointer to the embedded data and writes its length to *length
    const uint8_t* get_embedded_data(size_t* length);
    
    // Returns a heap-allocated C string from Rust
    char* get_rust_greeting();
    
    // Frees the C string
    void free_rust_string(char* s);

    // Fetches data from URL (blocking)
    // Returns pointer to heap allocated bytes, writes length to out_len
    // Returns nullptr on failure
    uint8_t* fetch_url_data(const char* url, size_t* out_len);

    // Frees the bytes returned by fetch_url_data
    void free_rust_bytes(uint8_t* ptr, size_t len);
}
