#include "ImguiRender.hpp"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <array>
#include <mutex>

namespace {
    // Reserve low SRV slots for ImGui backend allocations.
    // ImageLoaderSystem starts dynamic texture descriptors at index 10.
    constexpr UINT kImguiSrvPoolStart = 0;
    constexpr UINT kImguiSrvPoolCount = 10;

    struct DescriptorPoolState {
        ID3D12DescriptorHeap* Heap = nullptr;
        ID3D12Device* Device = nullptr;
        UINT Increment = 0;
        int PersistentFontIndex = -1;
        std::array<bool, kImguiSrvPoolCount> Used = {};
        bool Initialized = false;
    };

    std::mutex s_DescriptorPoolMutex;
    DescriptorPoolState s_DescriptorPool;

    void ResetDescriptorPoolLocked() {
        s_DescriptorPool = {};
    }

    void EnsureDescriptorPoolInitializedLocked(ImGui_ImplDX12_InitInfo* initInfo) {
        if (!initInfo || !initInfo->SrvDescriptorHeap || !initInfo->Device) {
            return;
        }

        if (s_DescriptorPool.Initialized &&
            s_DescriptorPool.Heap == initInfo->SrvDescriptorHeap &&
            s_DescriptorPool.Device == initInfo->Device) {
            return;
        }

        ResetDescriptorPoolLocked();
        s_DescriptorPool.Heap = initInfo->SrvDescriptorHeap;
        s_DescriptorPool.Device = initInfo->Device;
        s_DescriptorPool.Increment = initInfo->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        s_DescriptorPool.Initialized = s_DescriptorPool.Increment != 0;
    }

    bool ResolveSrvIndexFromCpuHandleLocked(D3D12_CPU_DESCRIPTOR_HANDLE handle, int& outIndex) {
        outIndex = -1;
        if (!s_DescriptorPool.Initialized || !s_DescriptorPool.Heap || s_DescriptorPool.Increment == 0 || handle.ptr == 0) {
            return false;
        }

        const D3D12_CPU_DESCRIPTOR_HANDLE heapStart = s_DescriptorPool.Heap->GetCPUDescriptorHandleForHeapStart();
        if (handle.ptr < heapStart.ptr) {
            return false;
        }

        const SIZE_T delta = handle.ptr - heapStart.ptr;
        if ((delta % s_DescriptorPool.Increment) != 0) {
            return false;
        }

        const SIZE_T rawIndex = delta / s_DescriptorPool.Increment;
        if (rawIndex >= (kImguiSrvPoolStart + kImguiSrvPoolCount)) {
            return false;
        }

        outIndex = static_cast<int>(rawIndex);
        return true;
    }

    void ImGuiSrvAlloc(ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
        std::lock_guard<std::mutex> lock(s_DescriptorPoolMutex);
        EnsureDescriptorPoolInitializedLocked(initInfo);
        if (!s_DescriptorPool.Initialized || !s_DescriptorPool.Heap || s_DescriptorPool.Increment == 0) {
            if (outCpu) {
                *outCpu = {};
            }
            if (outGpu) {
                *outGpu = {};
            }
            return;
        }

        int chosen = -1;
        for (UINT i = 0; i < kImguiSrvPoolCount; ++i) {
            const int candidate = static_cast<int>(kImguiSrvPoolStart + i);
            const UINT poolIndex = static_cast<UINT>(candidate - static_cast<int>(kImguiSrvPoolStart));
            if (!s_DescriptorPool.Used[poolIndex]) {
                chosen = candidate;
                break;
            }
        }

        if (chosen < 0) {
            chosen = s_DescriptorPool.PersistentFontIndex >= 0
                ? s_DescriptorPool.PersistentFontIndex
                : static_cast<int>(kImguiSrvPoolStart);
        }

        const UINT chosenPoolIndex = static_cast<UINT>(chosen - static_cast<int>(kImguiSrvPoolStart));
        if (chosenPoolIndex < kImguiSrvPoolCount) {
            s_DescriptorPool.Used[chosenPoolIndex] = true;
        }

        if (s_DescriptorPool.PersistentFontIndex < 0) {
            s_DescriptorPool.PersistentFontIndex = chosen;
        }

        const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = s_DescriptorPool.Heap->GetCPUDescriptorHandleForHeapStart();
        const D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = s_DescriptorPool.Heap->GetGPUDescriptorHandleForHeapStart();
        const SIZE_T offset = static_cast<SIZE_T>(chosen) * s_DescriptorPool.Increment;

        if (outCpu) {
            *outCpu = cpuStart;
            outCpu->ptr += offset;
        }
        if (outGpu) {
            *outGpu = gpuStart;
            outGpu->ptr += offset;
        }
    }

    void ImGuiSrvFree(ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE) {
        std::lock_guard<std::mutex> lock(s_DescriptorPoolMutex);
        EnsureDescriptorPoolInitializedLocked(initInfo);
        if (!s_DescriptorPool.Initialized) {
            return;
        }

        int srvIndex = -1;
        if (!ResolveSrvIndexFromCpuHandleLocked(cpuHandle, srvIndex)) {
            return;
        }

        // Keep font descriptor stable across atlas rebuilds to avoid reuse hazards.
        if (srvIndex == s_DescriptorPool.PersistentFontIndex) {
            return;
        }

        const UINT poolIndex = static_cast<UINT>(srvIndex - static_cast<int>(kImguiSrvPoolStart));
        if (poolIndex < kImguiSrvPoolCount) {
            s_DescriptorPool.Used[poolIndex] = false;
        }
    }
}

namespace ImguiRender {
    void Init(HWND hWnd, ID3D12Device* device, int num_frames, DXGI_FORMAT rtv_format, ID3D12DescriptorHeap* srv_heap, ID3D12CommandQueue* command_queue) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(hWnd);
        
        ImGui_ImplDX12_InitInfo init_info = {};
        init_info.Device = device;
        init_info.CommandQueue = command_queue;
        init_info.NumFramesInFlight = num_frames;
        init_info.RTVFormat = rtv_format;
        init_info.SrvDescriptorHeap = srv_heap;
        init_info.SrvDescriptorAllocFn = ImGuiSrvAlloc;
        init_info.SrvDescriptorFreeFn = ImGuiSrvFree;
        
        ImGui_ImplDX12_Init(&init_info);
    }

    void NewFrame() {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }
    
    void RenderDrawData(ID3D12GraphicsCommandList* command_list) {
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);
    }

    void Cleanup() {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        std::lock_guard<std::mutex> lock(s_DescriptorPoolMutex);
        ResetDescriptorPoolLocked();
    }
}
