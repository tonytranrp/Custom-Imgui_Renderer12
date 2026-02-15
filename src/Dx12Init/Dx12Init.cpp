#include "Dx12Init.hpp"
#include <iostream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace DX12Init {
    
    // Global variable definitions
    FrameContext                 g_frameContext[NUM_FRAMES_IN_FLIGHT] = {};
    UINT                         g_frameIndex = 0;

    ID3D12Device*                g_pd3dDevice = nullptr;
    ID3D12DescriptorHeap*        g_pd3dRtvDescHeap = nullptr;
    ID3D12DescriptorHeap*        g_pd3dSrvDescHeap = nullptr;
    ID3D12CommandQueue*          g_pd3dCommandQueue = nullptr;
    ID3D12GraphicsCommandList*   g_pd3dCommandList = nullptr;
    IDXGISwapChain3*             g_pSwapChain = nullptr;
    ID3D12Resource*              g_mainRenderTargetResource[NUM_BACK_BUFFERS] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};
    
    // Internal synchronization
    ID3D12Fence*                 g_fence = nullptr;
    HANDLE                       g_fenceEvent = nullptr;
    UINT64                       g_fenceLastSignaledValue = 0;
    HANDLE                       g_hSwapChainWaitableObject = nullptr;

    bool CreateDeviceD3D(HWND hWnd) {
        // Setup swap chain
        DXGI_SWAP_CHAIN_DESC1 sd;
        {
            ZeroMemory(&sd, sizeof(sd));
            sd.BufferCount = NUM_BACK_BUFFERS;
            sd.Width = 0;
            sd.Height = 0;
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.SampleDesc.Count = 1;
            sd.SampleDesc.Quality = 0;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
            sd.Scaling = DXGI_SCALING_STRETCH;
            sd.Stereo = FALSE;
        }

        // [DEBUG] Enable debug interface
#ifdef DX12_ENABLE_DEBUG_LAYER
        ID3D12Debug* pd3dDebug = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pd3dDebug))))
            pd3dDebug->EnableDebugLayer();
#endif

        // Create device
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&g_pd3dDevice)) != S_OK)
            return false;

        // [DEBUG] Setup debug info queue
#ifdef DX12_ENABLE_DEBUG_LAYER
        if (pd3dDebug != nullptr) {
            ID3D12InfoQueue* pInfoQueue = nullptr;
            g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
            pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
            pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            pInfoQueue->Release();
            pd3dDebug->Release();
        }
#endif

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = NUM_BACK_BUFFERS;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask = 1;
            if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap)) != S_OK)
                return false;

            SIZE_T rtvDescriptorSize = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
            for (UINT i = 0; i < NUM_BACK_BUFFERS; i++) {
                g_mainRenderTargetDescriptor[i] = rtvHandle;
                rtvHandle.ptr += rtvDescriptorSize;
            }
        }

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = 1;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
                return false;
        }

        {
            D3D12_COMMAND_QUEUE_DESC desc = {};
            desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.NodeMask = 1;
            if (g_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_pd3dCommandQueue)) != S_OK)
                return false;
        }

        for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
            if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)) != S_OK)
                return false;

        if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList)) != S_OK) {
            return false;
        }

        if (g_pd3dCommandList->Close() != S_OK)
            return false;

        if (g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK)
            return false;

        g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (g_fenceEvent == nullptr)
            return false;

        {
            IDXGIFactory4* dxgiFactory = nullptr;
            IDXGISwapChain1* swapChain1 = nullptr;
            if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK)
                return false;
            if (dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK)
                return false;
            if (swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain)) != S_OK)
                return false;
            swapChain1->Release();
            dxgiFactory->Release();
            g_pSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
            g_hSwapChainWaitableObject = g_pSwapChain->GetFrameLatencyWaitableObject();
        }

        CreateRenderTarget();
        return true;
    }

    void CleanupDeviceD3D() {
        CleanupRenderTarget();
        if (g_pSwapChain) { g_pSwapChain->SetFullscreenState(FALSE, nullptr); g_pSwapChain->Release(); g_pSwapChain = nullptr; }
        if (g_hSwapChainWaitableObject != nullptr) { CloseHandle(g_hSwapChainWaitableObject); }
        for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
            if (g_frameContext[i].CommandAllocator) { g_frameContext[i].CommandAllocator->Release(); g_frameContext[i].CommandAllocator = nullptr; }
        if (g_pd3dCommandQueue) { g_pd3dCommandQueue->Release(); g_pd3dCommandQueue = nullptr; }
        if (g_pd3dCommandList) { g_pd3dCommandList->Release(); g_pd3dCommandList = nullptr; }
        if (g_pd3dRtvDescHeap) { g_pd3dRtvDescHeap->Release(); g_pd3dRtvDescHeap = nullptr; }
        if (g_pd3dSrvDescHeap) { g_pd3dSrvDescHeap->Release(); g_pd3dSrvDescHeap = nullptr; }
        if (g_fence) { g_fence->Release(); g_fence = nullptr; }
        if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
        if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    }

    void CreateRenderTarget() {
        for (UINT i = 0; i < NUM_BACK_BUFFERS; i++) {
            ID3D12Resource* pBackBuffer = nullptr;
            g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, g_mainRenderTargetDescriptor[i]);
            g_mainRenderTargetResource[i] = pBackBuffer;
        }
    }

    void CleanupRenderTarget() {
        WaitForLastSubmittedFrame();
        for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
            if (g_mainRenderTargetResource[i]) { g_mainRenderTargetResource[i]->Release(); g_mainRenderTargetResource[i] = nullptr; }
    }

    void WaitForLastSubmittedFrame() {
        FrameContext* frameCtx = &g_frameContext[g_frameIndex % NUM_FRAMES_IN_FLIGHT];
        UINT64 fenceValue = frameCtx->FenceValue;
        if (fenceValue == 0)
            return; // No fence was signaled
        if (g_fence->GetCompletedValue() >= fenceValue) {
            frameCtx->FenceValue = 0;
            return;
        }
        g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
        WaitForSingleObject(g_fenceEvent, INFINITE);
        frameCtx->FenceValue = 0;
    }

    FrameContext* WaitForNextFrameResources() {
        UINT nextFrameIndex = g_frameIndex + 1;
        g_frameIndex = nextFrameIndex;

        HANDLE waitableObjects[] = { g_hSwapChainWaitableObject, nullptr };
        DWORD numWaitableObjects = 1;

        FrameContext* frameCtx = &g_frameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
        UINT64 fenceValue = frameCtx->FenceValue;
        if (fenceValue != 0) // means no fence was signaled
        {
            frameCtx->FenceValue = 0;
            g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
            waitableObjects[1] = g_fenceEvent;
            numWaitableObjects = 2;
        }

        WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

        return frameCtx;
    }

    void ResizeSwapChain(HWND hWnd, int width, int height) {
        DXGI_SWAP_CHAIN_DESC1 sd;
        g_pSwapChain->GetDesc1(&sd);
        
        WaitForLastSubmittedFrame();
        CleanupRenderTarget();
        
        // Use ResizeBuffers1 as requested
        // We cast to IDXGISwapChain3 to access ResizeBuffers1
        // (g_pSwapChain is already IDXGISwapChain3*)
        
        // NodeMask array (usually just 1 entry with 0 or 1 for single GPU)
        // For basic resizing, passing 0 as node mask and 0 length queue array behaves like ResizeBuffers
        UINT nodeMasks[] = { 1 }; 
        IUnknown* commandQueues[] = { g_pd3dCommandQueue };
        
        // Note: ResizeBuffers1 is typically used when you need to change the swap chain's node association or command queues.
        // If we just want to resize, ResizeBuffers is sufficient.
        // BUT, the user explicitly asked for "initlize the resizebuffers1".
        
        HRESULT result = g_pSwapChain->ResizeBuffers1(
            0,              // BufferCount (0 = preserve existing)
            width,          // Width
            height,         // Height
            DXGI_FORMAT_UNKNOWN, // Format (UNKNOWN = preserve existing)
            sd.Flags,       // Flags
            nullptr,        // pCreationNodeMask
            nullptr         // ppPresentQueue
        );
        
        if (FAILED(result)) {
            // Fallback to standard ResizeBuffers if ResizeBuffers1 fails or is not supported (unlikely on DX12)
             result = g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, sd.Flags);
        }
        
        if (FAILED(result)) {
             fprintf(stderr, "ResizeBuffers failed: %08X\n", result);
        }
        
        CreateRenderTarget();
    }
}
