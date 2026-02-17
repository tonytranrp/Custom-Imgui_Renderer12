#include "Dx12Init.hpp"
#include "Render/ImguiRender.hpp"
#include "Render/RenderUtils/FontSystem.hpp"
#include "Render/RenderUtils/ShaderSystem.hpp"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include <iostream>
#include <cmath>
#include <exception>
#include <string>
#include <wrl/client.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace DX12Init {

    namespace {
        using Microsoft::WRL::ComPtr;

        struct RuntimeState {
            const RunConfig* Config = nullptr;
            bool RequestExit = false;
            bool ImGuiInitialized = false;
            bool ShaderInitialized = false;
            bool DeviceInitialized = false;
            bool WindowCreated = false;
            bool ClassRegistered = false;
            bool ExternalMode = false;
            bool ExternalFrameActive = false;
            bool ExternalShaderPassActive = false;
            HWND WindowHandle = nullptr;
            ExternalRuntimeConfig ExternalConfig = {};
            D3D12_CPU_DESCRIPTOR_HANDLE ExternalCurrentRTV = {};
            ImVec2 ExternalDisplaySize = ImVec2(0.0f, 0.0f);
            UINT ExternalBackBufferIndex = 0;
            float ExternalDeltaTime = 1.0f / 60.0f;
        };

        RuntimeState s_RuntimeState{};

        void LogRuntimeError(const char* stage, const char* message) {
            std::string line = "[DX12Init::RunApp] ";
            line += stage ? stage : "UnknownStage";
            line += ": ";
            line += message ? message : "Unknown error";
            line += "\n";
            OutputDebugStringA(line.c_str());
            std::cerr << line;
        }

        LRESULT WINAPI RuntimeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
            if (s_RuntimeState.Config && s_RuntimeState.Config->MessageHook) {
                bool handled = false;
                const LRESULT hookResult = s_RuntimeState.Config->MessageHook(hWnd, msg, wParam, lParam, handled);
                if (handled) {
                    return hookResult;
                }
            }

            if (s_RuntimeState.ImGuiInitialized && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
                return 1;
            }

            switch (msg) {
            case WM_SIZE:
                if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
                    ResizeSwapChain(hWnd, static_cast<int>(LOWORD(lParam)), static_cast<int>(HIWORD(lParam)));
                }
                return 0;
            case WM_SYSCOMMAND:
                if ((wParam & 0xfff0) == SC_KEYMENU) {
                    return 0;
                }
                break;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                break;
            }
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }

        void ResetExternalBindings() {
            g_pd3dCommandList = nullptr;
            g_pd3dCommandQueue = nullptr;
            g_pd3dSrvDescHeap = nullptr;
            g_pd3dRtvDescHeap = nullptr;
            g_pd3dDevice = nullptr;
            g_pSwapChain = nullptr;
            g_hSwapChainWaitableObject = nullptr;
            g_fence = nullptr;
            g_fenceEvent = nullptr;
            g_fenceLastSignaledValue = 0;
            for (UINT i = 0; i < NUM_BACK_BUFFERS; ++i) {
                g_mainRenderTargetResource[i] = nullptr;
                g_mainRenderTargetDescriptor[i] = {};
            }
        }

        float SanitizeDeltaTime(float dt, ImGuiIO* ioFallback = nullptr) {
            if (std::isfinite(dt) && dt > 0.0f) {
                if (dt > 0.25f) {
                    dt = 0.25f;
                }
                return dt;
            }

            if (ioFallback && std::isfinite(ioFallback->Framerate) && ioFallback->Framerate > 1.0f) {
                dt = 1.0f / ioFallback->Framerate;
                if (dt > 0.25f) {
                    dt = 0.25f;
                }
                return dt;
            }
            return 1.0f / 60.0f;
        }

        void UnwindRuntime() {
            if (s_RuntimeState.ExternalMode) {
                if (s_RuntimeState.ShaderInitialized) {
                    RenderUtils::ShaderSystem::Shutdown();
                    s_RuntimeState.ShaderInitialized = false;
                }
                if (s_RuntimeState.ImGuiInitialized) {
                    ImguiRender::Cleanup();
                    s_RuntimeState.ImGuiInitialized = false;
                }
                s_RuntimeState.ExternalFrameActive = false;
                s_RuntimeState.ExternalShaderPassActive = false;
                s_RuntimeState.ExternalConfig = {};
                ResetExternalBindings();
                s_RuntimeState.ExternalMode = false;
                s_RuntimeState.Config = nullptr;
                s_RuntimeState.WindowHandle = nullptr;
                return;
            }

            if (s_RuntimeState.DeviceInitialized) {
                WaitForLastSubmittedFrame();
            }

            if (s_RuntimeState.ShaderInitialized) {
                RenderUtils::ShaderSystem::Shutdown();
                s_RuntimeState.ShaderInitialized = false;
            }
            if (s_RuntimeState.ImGuiInitialized) {
                ImguiRender::Cleanup();
                s_RuntimeState.ImGuiInitialized = false;
            }
            if (s_RuntimeState.DeviceInitialized) {
                CleanupDeviceD3D();
                s_RuntimeState.DeviceInitialized = false;
            }

            if (s_RuntimeState.WindowCreated && s_RuntimeState.WindowHandle) {
                DestroyWindow(s_RuntimeState.WindowHandle);
                s_RuntimeState.WindowHandle = nullptr;
                s_RuntimeState.WindowCreated = false;
            }
            if (s_RuntimeState.ClassRegistered && s_RuntimeState.Config) {
                UnregisterClassW(s_RuntimeState.Config->Window.ClassName.c_str(), GetModuleHandle(nullptr));
                s_RuntimeState.ClassRegistered = false;
            }
        }
    } // namespace
    
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
            desc.NumDescriptors = 128; // Increased for textures
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
            ComPtr<IDXGIFactory4> dxgiFactory;
            ComPtr<IDXGISwapChain1> swapChain1;
            if (CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf())) != S_OK)
                return false;
            if (dxgiFactory->CreateSwapChainForHwnd(
                g_pd3dCommandQueue, hWnd, &sd, nullptr, nullptr, swapChain1.GetAddressOf()) != S_OK)
                return false;
            if (swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain)) != S_OK)
                return false;
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
        if (s_RuntimeState.ExternalMode) {
            if (s_RuntimeState.ExternalConfig.WaitForGpuIdle) {
                s_RuntimeState.ExternalConfig.WaitForGpuIdle();
            }
            return;
        }

        if (!g_fence || !g_fenceEvent) {
            return;
        }

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
        (void)hWnd;
        DXGI_SWAP_CHAIN_DESC1 sd;
        g_pSwapChain->GetDesc1(&sd);
        
        WaitForLastSubmittedFrame();
        CleanupRenderTarget();
        
        // Use ResizeBuffers1 as requested
        // We cast to IDXGISwapChain3 to access ResizeBuffers1
        // (g_pSwapChain is already IDXGISwapChain3*)
        
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

    void RequestExit() {
        s_RuntimeState.RequestExit = true;
    }

    bool AttachExternalRuntime(const ExternalRuntimeConfig& config) {
        if (s_RuntimeState.ExternalMode || s_RuntimeState.DeviceInitialized || s_RuntimeState.WindowCreated) {
            return false;
        }
        if (!config.WindowHandle || !config.Device || !config.CommandQueue || !config.SrvHeap) {
            return false;
        }

        s_RuntimeState = {};
        s_RuntimeState.ExternalMode = true;
        s_RuntimeState.WindowHandle = config.WindowHandle;
        s_RuntimeState.ExternalConfig = config;
        if (s_RuntimeState.ExternalConfig.NumFramesInFlight <= 0) {
            s_RuntimeState.ExternalConfig.NumFramesInFlight = 1;
        }

        g_pd3dDevice = config.Device;
        g_pd3dCommandQueue = config.CommandQueue;
        g_pd3dSrvDescHeap = config.SrvHeap;
        g_pd3dCommandList = nullptr;
        g_pd3dRtvDescHeap = nullptr;
        g_pSwapChain = nullptr;

        if (config.AutoInitImGui) {
            ImguiRender::Init(
                config.WindowHandle,
                config.Device,
                s_RuntimeState.ExternalConfig.NumFramesInFlight,
                config.BackbufferFormat,
                config.SrvHeap,
                config.CommandQueue);
            s_RuntimeState.ImGuiInitialized = true;
        }

        if (config.AutoInitShaderSystem && config.AllowShaderSystem) {
            RenderUtils::ShaderSystem::Initialize(config.Device);
            s_RuntimeState.ShaderInitialized = true;
        }

        return true;
    }

    void DetachExternalRuntime() {
        if (!s_RuntimeState.ExternalMode) {
            return;
        }
        UnwindRuntime();
        s_RuntimeState = {};
    }

    bool BeginExternalFrame(const ExternalFrameInput& input, FramePacket& outPacket) {
        outPacket = {};
        try {
            if (!s_RuntimeState.ExternalMode || !input.CommandList) {
                return false;
            }
            if (s_RuntimeState.ExternalFrameActive) {
                return false;
            }

            s_RuntimeState.ExternalFrameActive = true;
            s_RuntimeState.ExternalCurrentRTV = input.CurrentRTV;
            s_RuntimeState.ExternalBackBufferIndex = input.BackBufferIndex;
            g_pd3dCommandList = input.CommandList;

            ImGuiIO* io = nullptr;
            ImVec2 displaySize = input.DisplaySize;
            if (s_RuntimeState.ImGuiInitialized) {
                if (s_RuntimeState.ExternalConfig.AllowFontAtlasRebuild) {
                    RenderUtils::FontSystem::ProcessPendingAtlasRebuild();
                }
                ImguiRender::NewFrame();
                io = &ImGui::GetIO();
                if (displaySize.x <= 0.0f || displaySize.y <= 0.0f) {
                    displaySize = io->DisplaySize;
                }
            }

            const float deltaTime = SanitizeDeltaTime(input.DeltaTime, io);
            s_RuntimeState.ExternalDisplaySize = displaySize;
            s_RuntimeState.ExternalDeltaTime = deltaTime;

            if (s_RuntimeState.ShaderInitialized &&
                s_RuntimeState.ImGuiInitialized &&
                s_RuntimeState.ExternalConfig.AllowShaderSystem) {
                RenderUtils::ShaderSystem::BeginImGuiPass(
                    input.CommandList,
                    input.CurrentRTV,
                    displaySize);
                s_RuntimeState.ExternalShaderPassActive = true;
            }

            outPacket.WindowHandle = s_RuntimeState.WindowHandle;
            outPacket.Frame = nullptr;
            outPacket.BackBufferIndex = input.BackBufferIndex;
            outPacket.CommandList = input.CommandList;
            outPacket.IO = io;
            outPacket.DeltaTime = deltaTime;
            return true;
        } catch (const std::exception& ex) {
            LogRuntimeError("BeginExternalFrame", ex.what());
        } catch (...) {
            LogRuntimeError("BeginExternalFrame", "Unhandled non-standard exception.");
        }

        if (s_RuntimeState.ExternalShaderPassActive) {
            RenderUtils::ShaderSystem::EndImGuiPass();
            s_RuntimeState.ExternalShaderPassActive = false;
        }
        s_RuntimeState.ExternalFrameActive = false;
        return false;
    }

    void EndExternalFrame() {
        if (!s_RuntimeState.ExternalMode || !s_RuntimeState.ExternalFrameActive) {
            return;
        }

        try {
            if (s_RuntimeState.ImGuiInitialized && g_pd3dCommandList) {
                ImGui::Render();
                ImguiRender::RenderDrawData(g_pd3dCommandList);
            }
        } catch (const std::exception& ex) {
            LogRuntimeError("EndExternalFrame", ex.what());
        } catch (...) {
            LogRuntimeError("EndExternalFrame", "Unhandled non-standard exception.");
        }

        if (s_RuntimeState.ExternalShaderPassActive) {
            RenderUtils::ShaderSystem::EndImGuiPass();
            s_RuntimeState.ExternalShaderPassActive = false;
        }
        s_RuntimeState.ExternalFrameActive = false;
    }

    int RunApp(HINSTANCE instance, const RunConfig& runConfig, const RuntimeCallbacks& callbacks) {
        if (s_RuntimeState.ExternalMode) {
            DetachExternalRuntime();
        }

        s_RuntimeState = {};
        s_RuntimeState.Config = &runConfig;
        s_RuntimeState.RequestExit = false;

        const HINSTANCE hInst = instance ? instance : GetModuleHandle(nullptr);
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = runConfig.Window.ClassStyle;
        wc.lpfnWndProc = RuntimeWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = runConfig.Window.ClassName.c_str();

        if (!RegisterClassExW(&wc)) {
            return 1;
        }
        s_RuntimeState.ClassRegistered = true;

        HWND hWnd = CreateWindowExW(
            runConfig.Window.WindowExStyle,
            runConfig.Window.ClassName.c_str(),
            runConfig.Window.Title.c_str(),
            runConfig.Window.WindowStyle,
            runConfig.Window.PosX,
            runConfig.Window.PosY,
            runConfig.Window.Width,
            runConfig.Window.Height,
            nullptr,
            nullptr,
            hInst,
            nullptr);
        if (hWnd == nullptr) {
            UnwindRuntime();
            return 1;
        }
        s_RuntimeState.WindowHandle = hWnd;
        s_RuntimeState.WindowCreated = true;

        if (!CreateDeviceD3D(hWnd)) {
            UnwindRuntime();
            return 1;
        }
        s_RuntimeState.DeviceInitialized = true;

        if (runConfig.AutoInitImGui) {
            ImguiRender::Init(
                hWnd,
                g_pd3dDevice,
                NUM_FRAMES_IN_FLIGHT,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                g_pd3dSrvDescHeap,
                g_pd3dCommandQueue);
            s_RuntimeState.ImGuiInitialized = true;
        }

        if (runConfig.AutoInitShaderSystem) {
            RenderUtils::ShaderSystem::Initialize(g_pd3dDevice);
            s_RuntimeState.ShaderInitialized = true;
        }

        if (runConfig.AutoShowWindow) {
            ShowWindow(hWnd, runConfig.Window.ShowCmd);
            UpdateWindow(hWnd);
        }

        bool setupSucceeded = true;
        if (callbacks.OnSetup) {
            try {
                callbacks.OnSetup(hWnd);
            } catch (const std::exception& ex) {
                LogRuntimeError("OnSetup", ex.what());
                setupSucceeded = false;
            } catch (...) {
                LogRuntimeError("OnSetup", "Unhandled non-standard exception.");
                setupSucceeded = false;
            }
        }
        if (!setupSucceeded) {
            UnwindRuntime();
            s_RuntimeState = {};
            return 1;
        }

        bool done = false;
        while (!done && !s_RuntimeState.RequestExit) {
            MSG msg;
            while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (msg.message == WM_QUIT) {
                    done = true;
                }
            }
            if (done || s_RuntimeState.RequestExit) {
                break;
            }

            FrameContext* frameCtx = WaitForNextFrameResources();
            if (!frameCtx || !frameCtx->CommandAllocator || !g_pd3dCommandList || !g_pSwapChain) {
                break;
            }
            const UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
            frameCtx->CommandAllocator->Reset();
            g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);

            ImGuiIO* io = nullptr;
            if (s_RuntimeState.ImGuiInitialized) {
                RenderUtils::FontSystem::ProcessPendingAtlasRebuild();
                ImguiRender::NewFrame();
                io = &ImGui::GetIO();
            }

            float deltaTime = 1.0f / 60.0f;
            if (io && std::isfinite(io->Framerate) && io->Framerate > 1.0f) {
                deltaTime = 1.0f / io->Framerate;
            }
            if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
                deltaTime = 1.0f / 60.0f;
            }
            if (deltaTime > 0.25f) {
                deltaTime = 0.25f;
            }

            const ImVec2 displaySize = io ? io->DisplaySize : ImVec2(static_cast<float>(runConfig.Window.Width), static_cast<float>(runConfig.Window.Height));
            bool shaderPassActive = false;
            if (s_RuntimeState.ShaderInitialized && s_RuntimeState.ImGuiInitialized) {
                RenderUtils::ShaderSystem::BeginImGuiPass(
                    g_pd3dCommandList,
                    g_mainRenderTargetDescriptor[backBufferIdx],
                    displaySize);
                shaderPassActive = true;
            }

            FramePacket packet;
            packet.WindowHandle = hWnd;
            packet.Frame = frameCtx;
            packet.BackBufferIndex = backBufferIdx;
            packet.CommandList = g_pd3dCommandList;
            packet.IO = io;
            packet.DeltaTime = deltaTime;
            if (callbacks.OnFrame) {
                try {
                    callbacks.OnFrame(packet);
                } catch (const std::exception& ex) {
                    if (shaderPassActive) {
                        RenderUtils::ShaderSystem::EndImGuiPass();
                        shaderPassActive = false;
                    }
                    LogRuntimeError("OnFrame", ex.what());
                    done = true;
                    s_RuntimeState.RequestExit = true;
                    continue;
                } catch (...) {
                    if (shaderPassActive) {
                        RenderUtils::ShaderSystem::EndImGuiPass();
                        shaderPassActive = false;
                    }
                    LogRuntimeError("OnFrame", "Unhandled non-standard exception.");
                    done = true;
                    s_RuntimeState.RequestExit = true;
                    continue;
                }
            }

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            g_pd3dCommandList->ResourceBarrier(1, &barrier);

            g_pd3dCommandList->ClearRenderTargetView(
                g_mainRenderTargetDescriptor[backBufferIdx],
                runConfig.ClearColor,
                0,
                nullptr);
            g_pd3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
            g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);

            if (s_RuntimeState.ImGuiInitialized) {
                ImGui::Render();
                ImguiRender::RenderDrawData(g_pd3dCommandList);
            }
            if (shaderPassActive) {
                RenderUtils::ShaderSystem::EndImGuiPass();
                shaderPassActive = false;
            }

            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            g_pd3dCommandList->ResourceBarrier(1, &barrier);
            g_pd3dCommandList->Close();

            g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);
            g_pSwapChain->Present(runConfig.VSync ? 1 : 0, 0);

            const UINT64 fenceValue = g_fenceLastSignaledValue + 1;
            g_pd3dCommandQueue->Signal(g_fence, fenceValue);
            g_fenceLastSignaledValue = fenceValue;
            frameCtx->FenceValue = fenceValue;
        }

        WaitForLastSubmittedFrame();
        if (callbacks.OnShutdown) {
            try {
                callbacks.OnShutdown();
            } catch (const std::exception& ex) {
                LogRuntimeError("OnShutdown", ex.what());
            } catch (...) {
                LogRuntimeError("OnShutdown", "Unhandled non-standard exception.");
            }
        }

        UnwindRuntime();
        s_RuntimeState = {};
        return 0;
    }
}
