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
}
