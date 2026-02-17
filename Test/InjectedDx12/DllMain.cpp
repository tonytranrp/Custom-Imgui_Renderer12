#include <Windows.h>

#include "DirectX12HookRuntime.hpp"

namespace {
    DWORD WINAPI HookBootstrapThread(LPVOID) {
        try {
            TestInjectedDx12::HookRuntime::Start();
        } catch (...) {
        }
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(module);
        HANDLE worker = CreateThread(nullptr, 0, HookBootstrapThread, nullptr, 0, nullptr);
        if (worker != nullptr) {
            CloseHandle(worker);
        }
        break;
    }
    case DLL_PROCESS_DETACH:
        TestInjectedDx12::HookRuntime::Stop();
        break;
    default:
        break;
    }
    return TRUE;
}
