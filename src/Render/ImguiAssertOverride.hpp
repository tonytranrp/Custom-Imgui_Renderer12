#pragma once

#ifdef IMGUI_DX12_TESTHOOK_NON_FATAL_ASSERTS

#include <Windows.h>
#include <cstdio>

inline void ImGuiTestHookNonFatalAssertHandler(const char* expression, const char* file, int line) {
    char buffer[512] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "[ImGuiDX12TestHook] Non-fatal IM_ASSERT: %s (%s:%d)\n",
        expression ? expression : "<null>",
        file ? file : "<unknown>",
        line);
    OutputDebugStringA(buffer);
}

#ifdef IM_ASSERT
#undef IM_ASSERT
#endif

#define IM_ASSERT(_EXPR) \
    do { \
        if (!(_EXPR)) { \
            ImGuiTestHookNonFatalAssertHandler(#_EXPR, __FILE__, __LINE__); \
        } \
    } while (0)

#endif
