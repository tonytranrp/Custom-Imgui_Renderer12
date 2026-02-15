#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <tchar.h>

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

// Forward declarations
struct FrameContext {
    ID3D12CommandAllocator* CommandAllocator;
    UINT64                  FenceValue;
};

namespace DX12Init {

    // Constants
    constexpr int NUM_BACK_BUFFERS = 3;
    constexpr int NUM_FRAMES_IN_FLIGHT = 3;

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
    
    // Descriptor Management
    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuSrvHandle(int index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuSrvHandle(int index);
}
