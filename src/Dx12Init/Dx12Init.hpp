#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <tchar.h>
#include <functional>
#include <string>
#include "imgui.h"

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

// Forward declarations
struct FrameContext {
    ID3D12CommandAllocator* CommandAllocator;
    UINT64                  FenceValue;
};

namespace DX12Init {

    struct WindowConfig {
        std::wstring ClassName = L"ImGui DX12 Class";
        std::wstring Title = L"ImGui DX12 App";
        int PosX = 100;
        int PosY = 100;
        int Width = 1600;
        int Height = 900;
        DWORD ClassStyle = CS_CLASSDC;
        DWORD WindowStyle = WS_OVERLAPPEDWINDOW;
        DWORD WindowExStyle = 0;
        int ShowCmd = SW_SHOWDEFAULT;
    };

    struct RunConfig {
        WindowConfig Window;
        float ClearColor[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
        bool VSync = true;
        bool AutoInitImGui = true;
        bool AutoInitShaderSystem = true;
        bool AutoShowWindow = true;
        std::function<LRESULT(HWND, UINT, WPARAM, LPARAM, bool&)> MessageHook;
    };

    struct FramePacket {
        HWND WindowHandle = nullptr;
        FrameContext* Frame = nullptr;
        UINT BackBufferIndex = 0;
        ID3D12GraphicsCommandList* CommandList = nullptr;
        ImGuiIO* IO = nullptr;
        float DeltaTime = 1.0f / 60.0f;
    };

    struct RuntimeCallbacks {
        std::function<void(HWND)> OnSetup;
        std::function<void(const FramePacket&)> OnFrame;
        std::function<void()> OnShutdown;
    };

    // Constants
    constexpr int NUM_BACK_BUFFERS = 3;
    constexpr int NUM_FRAMES_IN_FLIGHT = 3;

    struct ExternalRuntimeConfig {
        HWND WindowHandle = nullptr;
        ID3D12Device* Device = nullptr;
        ID3D12CommandQueue* CommandQueue = nullptr;
        ID3D12DescriptorHeap* SrvHeap = nullptr;
        DXGI_FORMAT BackbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        int NumFramesInFlight = NUM_FRAMES_IN_FLIGHT;
        bool AutoInitImGui = true;
        bool AutoInitShaderSystem = true;
        bool AllowFontAtlasRebuild = true;
        bool AllowShaderSystem = true;
        std::function<void()> WaitForGpuIdle;
    };

    struct ExternalFrameInput {
        ID3D12GraphicsCommandList* CommandList = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE CurrentRTV = {};
        ImVec2 DisplaySize = ImVec2(0.0f, 0.0f);
        UINT BackBufferIndex = 0;
        float DeltaTime = 1.0f / 60.0f;
    };

    // Global DX12 State (declared as extern)
    extern ID3D12Device*                g_pd3dDevice;
    extern ID3D12DescriptorHeap*        g_pd3dRtvDescHeap;
    extern ID3D12DescriptorHeap*        g_pd3dSrvDescHeap;
    extern ID3D12CommandQueue*          g_pd3dCommandQueue;
    extern ID3D12GraphicsCommandList*   g_pd3dCommandList;
    extern IDXGISwapChain3*             g_pSwapChain;
    extern ID3D12Resource*              g_mainRenderTargetResource[NUM_BACK_BUFFERS];
    extern D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor[NUM_BACK_BUFFERS];
    
    // Internal synchronization
    extern ID3D12Fence*                 g_fence;
    extern HANDLE                       g_fenceEvent;
    extern UINT64                       g_fenceLastSignaledValue;
    extern HANDLE                       g_hSwapChainWaitableObject;

    // Core Functions
    bool CreateDeviceD3D(HWND hWnd);
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void WaitForLastSubmittedFrame();
    FrameContext* WaitForNextFrameResources();
    void ResizeSwapChain(HWND hWnd, int width, int height);
    int RunApp(HINSTANCE instance, const RunConfig& runConfig, const RuntimeCallbacks& callbacks);
    void RequestExit();

    bool AttachExternalRuntime(const ExternalRuntimeConfig& config);
    void DetachExternalRuntime();
    bool BeginExternalFrame(const ExternalFrameInput& input, FramePacket& outPacket);
    void EndExternalFrame();
    
    // Descriptor Management
    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuSrvHandle(int index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuSrvHandle(int index);
}
