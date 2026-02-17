#include "Dx12Init/ExternalOverlayRuntime.hpp"

#include <Windows.h>
#include <windowsx.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

#include "Dx12Init/Dx12Init.hpp"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace DX12Init {
    namespace {
        struct QueuedMessage {
            HWND Window = nullptr;
            UINT Message = 0;
            WPARAM WParam = 0;
            LPARAM LParam = 0;
        };

        constexpr size_t kMaxQueuedInputMessages = 2048;

        struct InputRingBuffer {
            std::array<QueuedMessage, kMaxQueuedInputMessages> Messages = {};
            size_t Head = 0;
            size_t Tail = 0;
            size_t Count = 0;

            void Clear() {
                Head = 0;
                Tail = 0;
                Count = 0;
            }

            void Push(const QueuedMessage& msg) {
                Messages[Tail] = msg;
                Tail = (Tail + 1) % kMaxQueuedInputMessages;
                if (Count < kMaxQueuedInputMessages) {
                    ++Count;
                    return;
                }
                Head = (Head + 1) % kMaxQueuedInputMessages;
            }

            size_t Drain(std::array<QueuedMessage, kMaxQueuedInputMessages>& out) {
                size_t drained = 0;
                while (Count > 0 && drained < kMaxQueuedInputMessages) {
                    out[drained] = Messages[Head];
                    Head = (Head + 1) % kMaxQueuedInputMessages;
                    --Count;
                    ++drained;
                }
                return drained;
            }
        };

        struct RawInputState {
            POINT MousePosClient = {};
            bool HasMousePos = false;
            std::array<bool, 5> MouseButtons = {};
            float WheelX = 0.0f;
            float WheelY = 0.0f;
        };

        struct FrameResources {
            ID3D12CommandAllocator* Allocator = nullptr;
            ID3D12Resource* Backbuffer = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE RTV = {};
            UINT64 FenceValue = 0;
        };

        enum class HookStage : int {
            None = 0,
            Init,
            QueueCapture,
            AttachExternal,
            BeginFrame,
            Render,
            EndFrame,
            Resize,
            Shutdown
        };

        enum class InitializationState {
            Ready = 0,
            Waiting = 1,
            Failed = 2
        };

        constexpr int kRequiredStablePresentFrames = 2;

        std::atomic<bool> s_ShuttingDown = false;
        std::atomic<bool> s_Ready = false;
        std::atomic<bool> s_ExternalAttached = false;
        std::atomic<bool> s_SceneReady = false;
        std::atomic<bool> s_RenderReady = false;

        std::atomic<int> s_LastStage = static_cast<int>(HookStage::None);
        std::atomic<long> s_LastHResult = static_cast<long>(S_OK);

        HWND s_Window = nullptr;
        WNDPROC s_OriginalWndProc = nullptr;

        ID3D12Device* s_Device = nullptr;
        ID3D12CommandQueue* s_CommandQueue = nullptr;
        ID3D12CommandQueue* s_CandidateQueue = nullptr;
        ID3D12Device* s_CandidateQueueDevice = nullptr;
        int s_CandidateQueueSamples = 0;
        ID3D12CommandQueue* s_LastPresentQueue = nullptr;
        int s_StablePresentCount = 0;

        ID3D12GraphicsCommandList* s_CommandList = nullptr;
        ID3D12DescriptorHeap* s_RTVHeap = nullptr;
        ID3D12DescriptorHeap* s_SRVHeap = nullptr;
        ID3D12Fence* s_RuntimeFence = nullptr;
        HANDLE s_RuntimeFenceEvent = nullptr;
        UINT64 s_NextFenceValue = 1;

        DXGI_FORMAT s_BackbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        UINT s_BackbufferWidth = 0;
        UINT s_BackbufferHeight = 0;
        UINT s_BufferCount = 0;

        std::vector<FrameResources> s_Frames;

        std::mutex s_InputQueueMutex;
        InputRingBuffer s_InputQueue;
        RawInputState s_RawInputState;

        std::chrono::steady_clock::time_point s_LastFrameTime = std::chrono::steady_clock::now();

        ExternalOverlayRuntime::Config s_RuntimeConfig = {};
        ExternalOverlayRuntime* s_RuntimeInstance = nullptr;

        template <typename T>
        void SafeRelease(T*& ptr) {
            if (ptr != nullptr) {
                ptr->Release();
                ptr = nullptr;
            }
        }

        const char* StageName(HookStage stage) {
            switch (stage) {
            case HookStage::Init:
                return "Init";
            case HookStage::QueueCapture:
                return "QueueCapture";
            case HookStage::AttachExternal:
                return "AttachExternal";
            case HookStage::BeginFrame:
                return "BeginFrame";
            case HookStage::Render:
                return "Render";
            case HookStage::EndFrame:
                return "EndFrame";
            case HookStage::Resize:
                return "Resize";
            case HookStage::Shutdown:
                return "Shutdown";
            case HookStage::None:
            default:
                return "None";
            }
        }

        void SetStage(HookStage stage) {
            s_LastStage.store(static_cast<int>(stage));
        }

        HookStage GetStage() {
            return static_cast<HookStage>(s_LastStage.load());
        }

        void SetLastResult(HRESULT hr) {
            s_LastHResult.store(static_cast<long>(hr));
        }

        void LogHookMessage(const char* message) {
            if (message == nullptr) {
                return;
            }
            OutputDebugStringA("[ImGuiDX12TestHook] ");
            OutputDebugStringA(message);
            OutputDebugStringA("\n");
        }

        void LogStageMessage(const char* prefix, HRESULT hr = S_OK) {
            char buffer[512] = {};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "%s stage=%s hr=0x%08lX",
                prefix ? prefix : "[state]",
                StageName(GetStage()),
                static_cast<unsigned long>(hr));
            LogHookMessage(buffer);
        }

        void RestoreWndProc() {
            if (s_Window != nullptr && s_OriginalWndProc != nullptr) {
                SetWindowLongPtrW(s_Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_OriginalWndProc));
                s_OriginalWndProc = nullptr;
            }
        }

        void ResetQueueCaptureState() {
            s_CommandQueue = nullptr;
            s_CandidateQueue = nullptr;
            SafeRelease(s_CandidateQueueDevice);
            s_CandidateQueueSamples = 0;
            s_LastPresentQueue = nullptr;
            s_StablePresentCount = 0;
        }

        void WaitForQueueIdle() {
            if (s_RuntimeFence == nullptr || s_CommandQueue == nullptr || s_RuntimeFenceEvent == nullptr) {
                return;
            }

            const UINT64 fenceValue = s_NextFenceValue;
            if (FAILED(s_CommandQueue->Signal(s_RuntimeFence, fenceValue))) {
                return;
            }
            ++s_NextFenceValue;
            if (s_RuntimeFence->GetCompletedValue() >= fenceValue) {
                return;
            }
            if (FAILED(s_RuntimeFence->SetEventOnCompletion(fenceValue, s_RuntimeFenceEvent))) {
                return;
            }
            WaitForSingleObject(s_RuntimeFenceEvent, INFINITE);
        }

        void ClearQueuedInput() {
            std::lock_guard<std::mutex> lock(s_InputQueueMutex);
            s_InputQueue.Clear();
            s_RawInputState = {};
        }

        void DrainInputQueueOnRenderThread() {
            if (!s_Ready.load()) {
                ClearQueuedInput();
                return;
            }

            std::array<QueuedMessage, kMaxQueuedInputMessages> pending = {};
            size_t count = 0;
            {
                std::lock_guard<std::mutex> lock(s_InputQueueMutex);
                count = s_InputQueue.Drain(pending);
            }

            for (size_t i = 0; i < count; ++i) {
                const QueuedMessage& msg = pending[i];
                ImGui_ImplWin32_WndProcHandler(msg.Window, msg.Message, msg.WParam, msg.LParam);
            }
        }

        void ApplyRawInputFallback() {
            if (!s_RuntimeConfig.EnableRawInputFallback || !s_Ready.load() || s_Window == nullptr) {
                return;
            }
            if (ImGui::GetCurrentContext() == nullptr) {
                return;
            }

            ImGuiIO& io = ImGui::GetIO();
            RawInputState rawInput;
            {
                std::lock_guard<std::mutex> lock(s_InputQueueMutex);
                rawInput = s_RawInputState;
                s_RawInputState.WheelX = 0.0f;
                s_RawInputState.WheelY = 0.0f;
            }

            bool wroteMousePos = false;
            if (rawInput.HasMousePos) {
                io.AddMousePosEvent(static_cast<float>(rawInput.MousePosClient.x), static_cast<float>(rawInput.MousePosClient.y));
                wroteMousePos = true;
            }

            POINT cursor = {};
            if (!wroteMousePos && GetCursorPos(&cursor)) {
                POINT clientCursor = cursor;
                if (ScreenToClient(s_Window, &clientCursor)) {
                    io.AddMousePosEvent(static_cast<float>(clientCursor.x), static_cast<float>(clientCursor.y));
                } else {
                    RECT windowRect = {};
                    if (GetWindowRect(s_Window, &windowRect)) {
                        io.AddMousePosEvent(
                            static_cast<float>(cursor.x - windowRect.left),
                            static_cast<float>(cursor.y - windowRect.top));
                    }
                }
            }

            const auto keyDown = [](int vk) -> bool {
                return (GetAsyncKeyState(vk) & 0x8000) != 0;
            };
            const bool leftDown = rawInput.MouseButtons[0] || keyDown(VK_LBUTTON);
            const bool rightDown = rawInput.MouseButtons[1] || keyDown(VK_RBUTTON);
            const bool middleDown = rawInput.MouseButtons[2] || keyDown(VK_MBUTTON);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, leftDown);
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, rightDown);
            io.AddMouseButtonEvent(ImGuiMouseButton_Middle, middleDown);

            if (rawInput.WheelX != 0.0f || rawInput.WheelY != 0.0f) {
                io.AddMouseWheelEvent(rawInput.WheelX, rawInput.WheelY);
            }

            const HWND focusedWindow = GetForegroundWindow();
            const bool focused =
                focusedWindow == s_Window ||
                (focusedWindow != nullptr && IsChild(s_Window, focusedWindow)) ||
                (focusedWindow != nullptr && IsChild(focusedWindow, s_Window));
            io.AddFocusEvent(focused);
        }

        bool IsDirectQueue(ID3D12CommandQueue* queue) {
            if (queue == nullptr) {
                return false;
            }

            const D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
            return desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT;
        }

        bool IsQueueCompatibleWithSwapChain(ID3D12CommandQueue* queue, IDXGISwapChain* swapChain) {
            if (queue == nullptr || swapChain == nullptr) {
                return false;
            }

            ID3D12Device* queueDevice = nullptr;
            if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice == nullptr) {
                return false;
            }

            ID3D12Device* swapChainDevice = nullptr;
            const HRESULT swapHr = swapChain->GetDevice(IID_PPV_ARGS(&swapChainDevice));
            const bool compatible = SUCCEEDED(swapHr) && swapChainDevice != nullptr && queueDevice == swapChainDevice;

            if (swapChainDevice != nullptr) {
                swapChainDevice->Release();
            }
            queueDevice->Release();
            return compatible;
        }

        void CaptureQueueCandidateInternal(ID3D12CommandQueue* queue) {
            if (!queue || !IsDirectQueue(queue)) {
                return;
            }

            ID3D12Device* queueDevice = nullptr;
            if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice == nullptr) {
                return;
            }

            if (s_CandidateQueue != queue || s_CandidateQueueDevice != queueDevice) {
                SafeRelease(s_CandidateQueueDevice);
                s_CandidateQueue = queue;
                s_CandidateQueueDevice = queueDevice;
                s_CandidateQueueSamples = 1;
            } else {
                ++s_CandidateQueueSamples;
                queueDevice->Release();
            }

            if (s_CommandQueue == nullptr && s_CandidateQueueSamples >= 2) {
                s_CommandQueue = s_CandidateQueue;
            }
        }
        bool WaitForFrameFence(FrameResources& frame) {
            if (s_RuntimeFence == nullptr || s_RuntimeFenceEvent == nullptr || frame.FenceValue == 0) {
                return true;
            }

            if (s_RuntimeFence->GetCompletedValue() >= frame.FenceValue) {
                frame.FenceValue = 0;
                return true;
            }
            if (FAILED(s_RuntimeFence->SetEventOnCompletion(frame.FenceValue, s_RuntimeFenceEvent))) {
                return false;
            }
            WaitForSingleObject(s_RuntimeFenceEvent, INFINITE);
            frame.FenceValue = 0;
            return true;
        }

        void SignalFrameFence(FrameResources& frame) {
            if (s_RuntimeFence == nullptr || s_CommandQueue == nullptr) {
                return;
            }

            const UINT64 fenceValue = s_NextFenceValue;
            if (SUCCEEDED(s_CommandQueue->Signal(s_RuntimeFence, fenceValue))) {
                frame.FenceValue = fenceValue;
                ++s_NextFenceValue;
            }
        }

        bool IsAlwaysCapturedInputMessage(UINT message) {
            if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
                return true;
            }

            switch (message) {
            case WM_INPUT:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
            case WM_SYSCHAR:
            case WM_UNICHAR:
                return true;
            default:
                break;
            }
            return false;
        }

        LRESULT CALLBACK HookWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
            if (s_RuntimeInstance != nullptr) {
                bool handled = false;
                const LRESULT result = s_RuntimeInstance->OnWndProc(
                    hWnd,
                    message,
                    wParam,
                    lParam,
                    s_OriginalWndProc,
                    handled);
                if (handled) {
                    return result;
                }
            }

            if (s_OriginalWndProc != nullptr) {
                return CallWindowProcW(s_OriginalWndProc, hWnd, message, wParam, lParam);
            }
            return DefWindowProcW(hWnd, message, wParam, lParam);
        }

        void ReleaseRenderResources() {
            WaitForQueueIdle();

            for (FrameResources& frame : s_Frames) {
                frame.FenceValue = 0;
                SafeRelease(frame.Backbuffer);
                SafeRelease(frame.Allocator);
                frame.RTV = {};
            }
            s_Frames.clear();
            s_BufferCount = 0;

            SafeRelease(s_CommandList);
            SafeRelease(s_RTVHeap);
            SafeRelease(s_SRVHeap);
            SafeRelease(s_RuntimeFence);
            if (s_RuntimeFenceEvent != nullptr) {
                CloseHandle(s_RuntimeFenceEvent);
                s_RuntimeFenceEvent = nullptr;
            }
            s_NextFenceValue = 1;

            s_BackbufferWidth = 0;
            s_BackbufferHeight = 0;
            s_RenderReady.store(false);
        }

        void MarkRuntimeNotReady() {
            s_Ready.store(false);
            s_RenderReady.store(false);
            s_LastFrameTime = std::chrono::steady_clock::now();
            ClearQueuedInput();
        }

        bool BuildRenderResources(IDXGISwapChain* swapChain) {
            if (swapChain == nullptr || s_CommandQueue == nullptr) {
                return false;
            }
            if (s_RenderReady.load()) {
                return true;
            }

            if (!IsQueueCompatibleWithSwapChain(s_CommandQueue, swapChain)) {
                ResetQueueCaptureState();
                return false;
            }

            DXGI_SWAP_CHAIN_DESC desc = {};
            if (FAILED(swapChain->GetDesc(&desc))) {
                return false;
            }

            s_Window = desc.OutputWindow;
            if (s_Window == nullptr) {
                return false;
            }

            s_BackbufferFormat = desc.BufferDesc.Format == DXGI_FORMAT_UNKNOWN
                ? DXGI_FORMAT_R8G8B8A8_UNORM
                : desc.BufferDesc.Format;
            s_BackbufferWidth = desc.BufferDesc.Width;
            s_BackbufferHeight = desc.BufferDesc.Height;
            s_BufferCount = (std::max)(1u, desc.BufferCount);

            if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&s_Device))) || s_Device == nullptr) {
                return false;
            }

            D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
            rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvDesc.NumDescriptors = s_BufferCount;
            rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(s_Device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&s_RTVHeap))) || s_RTVHeap == nullptr) {
                return false;
            }

            D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
            srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            // Headroom for ImGui + font atlas rebuilds + image loader media descriptors in injected mode.
            srvDesc.NumDescriptors = 512;
            srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(s_Device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&s_SRVHeap))) || s_SRVHeap == nullptr) {
                return false;
            }

            if (FAILED(s_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s_RuntimeFence))) || s_RuntimeFence == nullptr) {
                return false;
            }
            s_RuntimeFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (s_RuntimeFenceEvent == nullptr) {
                return false;
            }
            s_NextFenceValue = 1;

            s_Frames.resize(s_BufferCount);
            const UINT rtvStep = s_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = s_RTVHeap->GetCPUDescriptorHandleForHeapStart();
            for (UINT i = 0; i < s_BufferCount; ++i) {
                s_Frames[i].RTV = rtvHandle;
                s_Frames[i].FenceValue = 0;
                rtvHandle.ptr += static_cast<SIZE_T>(rtvStep);

                if (FAILED(s_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s_Frames[i].Allocator))) ||
                    s_Frames[i].Allocator == nullptr) {
                    return false;
                }

                ID3D12Resource* backbuffer = nullptr;
                if (FAILED(swapChain->GetBuffer(i, IID_PPV_ARGS(&backbuffer))) || backbuffer == nullptr) {
                    return false;
                }
                s_Frames[i].Backbuffer = backbuffer;
                s_Device->CreateRenderTargetView(backbuffer, nullptr, s_Frames[i].RTV);
            }

            if (FAILED(s_Device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                s_Frames[0].Allocator,
                nullptr,
                IID_PPV_ARGS(&s_CommandList))) || s_CommandList == nullptr) {
                return false;
            }
            if (FAILED(s_CommandList->Close())) {
                return false;
            }

            if (s_OriginalWndProc == nullptr) {
                s_OriginalWndProc = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrW(s_Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookWndProc)));
            }

            s_LastFrameTime = std::chrono::steady_clock::now();
            s_RenderReady.store(true);
            return true;
        }
        bool AttachRenderRuntime() {
            if (s_ExternalAttached.load()) {
                return true;
            }

            SetStage(HookStage::AttachExternal);

            DX12Init::ExternalRuntimeConfig config;
            config.WindowHandle = s_Window;
            config.Device = s_Device;
            config.CommandQueue = s_CommandQueue;
            config.SrvHeap = s_SRVHeap;
            config.BackbufferFormat = s_BackbufferFormat;
            config.NumFramesInFlight = static_cast<int>((std::max)(1u, s_BufferCount));
            config.AutoInitImGui = s_RuntimeConfig.AutoInitImGui;
            config.AutoInitShaderSystem = s_RuntimeConfig.AutoInitShaderSystem;
            config.AllowFontAtlasRebuild = s_RuntimeConfig.AllowFontAtlasRebuild;
            config.AllowShaderSystem = s_RuntimeConfig.AllowShaderSystem;
            config.WaitForGpuIdle = []() {
                WaitForQueueIdle();
            };

            if (!DX12Init::AttachExternalRuntime(config)) {
                return false;
            }
            s_ExternalAttached.store(true);

            try {
                if (s_RuntimeConfig.OnSceneSetup) {
                    s_RuntimeConfig.OnSceneSetup(s_Window);
                }
            } catch (const std::exception& ex) {
                LogHookMessage(ex.what());
                DX12Init::DetachExternalRuntime();
                s_ExternalAttached.store(false);
                return false;
            } catch (...) {
                LogHookMessage("Scene setup callback threw an unknown exception.");
                DX12Init::DetachExternalRuntime();
                s_ExternalAttached.store(false);
                return false;
            }

            s_SceneReady.store(true);
            s_Ready.store(true);
            return true;
        }

        InitializationState EnsureInitialized(IDXGISwapChain* swapChain) {
            if (s_CommandQueue == nullptr) {
                return InitializationState::Waiting;
            }
            if (s_StablePresentCount < kRequiredStablePresentFrames) {
                return InitializationState::Waiting;
            }
            if (!BuildRenderResources(swapChain)) {
                if (s_CommandQueue == nullptr) {
                    return InitializationState::Waiting;
                }
                return InitializationState::Failed;
            }
            if (!AttachRenderRuntime()) {
                return InitializationState::Failed;
            }
            return InitializationState::Ready;
        }

        void ShutdownSceneAndRuntime() {
            s_Ready.store(false);
            if (s_SceneReady.exchange(false)) {
                try {
                    if (s_RuntimeConfig.OnSceneShutdown) {
                        s_RuntimeConfig.OnSceneShutdown();
                    }
                } catch (...) {
                }
            }
            if (s_ExternalAttached.exchange(false)) {
                try {
                    DX12Init::DetachExternalRuntime();
                } catch (...) {
                }
            }
        }

        void HandleResizeReset() {
            SetStage(HookStage::Resize);
            MarkRuntimeNotReady();
            ShutdownSceneAndRuntime();
            ReleaseRenderResources();
            SafeRelease(s_Device);
            ResetQueueCaptureState();
        }

        void DoFullShutdown() {
            SetStage(HookStage::Shutdown);
            s_ShuttingDown.store(true);
            MarkRuntimeNotReady();
            ShutdownSceneAndRuntime();
            RestoreWndProc();
            ReleaseRenderResources();
            SafeRelease(s_Device);
            ResetQueueCaptureState();
            s_Window = nullptr;
            s_BackbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        }

        void HandleOverlayFatal(const char* stageMessage, HRESULT hr = S_OK) {
            SetLastResult(hr);
            LogStageMessage(stageMessage ? stageMessage : "fatal", hr);
            s_ShuttingDown.store(true);
            MarkRuntimeNotReady();

            try {
                ShutdownSceneAndRuntime();
            } catch (...) {
            }

            try {
                RestoreWndProc();
            } catch (...) {
            }

            try {
                ReleaseRenderResources();
            } catch (...) {
            }

            SafeRelease(s_Device);
            ResetQueueCaptureState();
            s_Window = nullptr;
        }

        HRESULT HookPresent(
            IDXGISwapChain* swapChain,
            UINT syncInterval,
            UINT flags,
            ExternalOverlayRuntime::PresentFn originalPresent,
            ExternalOverlayRuntime::ExecuteCommandListsFn originalExecuteCommandLists) {
            SetStage(HookStage::BeginFrame);
            try {
                if (originalPresent == nullptr) {
                    return S_OK;
                }
                if (s_ShuttingDown.load() || swapChain == nullptr) {
                    return originalPresent(swapChain, syncInterval, flags);
                }

                if (s_CommandQueue != nullptr) {
                    if (s_LastPresentQueue == s_CommandQueue) {
                        ++s_StablePresentCount;
                    } else {
                        s_LastPresentQueue = s_CommandQueue;
                        s_StablePresentCount = 1;
                    }
                }

                const InitializationState initState = EnsureInitialized(swapChain);
                if (initState == InitializationState::Waiting) {
                    return originalPresent(swapChain, syncInterval, flags);
                }
                if (initState == InitializationState::Failed) {
                    HandleResizeReset();
                    return originalPresent(swapChain, syncInterval, flags);
                }

                IDXGISwapChain3* swapChain3 = nullptr;
                if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))) || swapChain3 == nullptr) {
                    return originalPresent(swapChain, syncInterval, flags);
                }

                const UINT backbufferIndex = swapChain3->GetCurrentBackBufferIndex();
                swapChain3->Release();
                if (backbufferIndex >= s_Frames.size()) {
                    return originalPresent(swapChain, syncInterval, flags);
                }

                FrameResources& frame = s_Frames[backbufferIndex];
                if (frame.Allocator == nullptr || frame.Backbuffer == nullptr || s_CommandList == nullptr) {
                    return originalPresent(swapChain, syncInterval, flags);
                }
                if (!WaitForFrameFence(frame)) {
                    return originalPresent(swapChain, syncInterval, flags);
                }

                const auto now = std::chrono::steady_clock::now();
                float deltaTime = std::chrono::duration<float>(now - s_LastFrameTime).count();
                s_LastFrameTime = now;
                if (!(deltaTime > 0.0f) || deltaTime > 0.5f) {
                    deltaTime = 1.0f / 60.0f;
                }

                D3D12_RESOURCE_DESC backbufferDesc = frame.Backbuffer->GetDesc();
                s_BackbufferWidth = static_cast<UINT>(backbufferDesc.Width);
                s_BackbufferHeight = backbufferDesc.Height;

                DrainInputQueueOnRenderThread();
                ApplyRawInputFallback();

                if (FAILED(frame.Allocator->Reset())) {
                    return originalPresent(swapChain, syncInterval, flags);
                }
                if (FAILED(s_CommandList->Reset(frame.Allocator, nullptr))) {
                    return originalPresent(swapChain, syncInterval, flags);
                }

                D3D12_RESOURCE_BARRIER toRT = {};
                toRT.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toRT.Transition.pResource = frame.Backbuffer;
                toRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                toRT.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                toRT.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                s_CommandList->ResourceBarrier(1, &toRT);

                s_CommandList->OMSetRenderTargets(1, &frame.RTV, FALSE, nullptr);
                if (s_SRVHeap != nullptr) {
                    ID3D12DescriptorHeap* heaps[] = { s_SRVHeap };
                    s_CommandList->SetDescriptorHeaps(1, heaps);
                }

                DX12Init::FramePacket framePacket;
                DX12Init::ExternalFrameInput frameInput;
                frameInput.CommandList = s_CommandList;
                frameInput.CurrentRTV = frame.RTV;
                frameInput.DisplaySize = ImVec2(static_cast<float>(s_BackbufferWidth), static_cast<float>(s_BackbufferHeight));
                frameInput.BackBufferIndex = backbufferIndex;
                frameInput.DeltaTime = deltaTime;

                const bool beganFrame = DX12Init::BeginExternalFrame(frameInput, framePacket);
                if (beganFrame) {
                    SetStage(HookStage::Render);
                    try {
                        if (s_RuntimeConfig.OnSceneFrame) {
                            s_RuntimeConfig.OnSceneFrame(framePacket);
                        }
                    } catch (...) {
                        LogHookMessage("Scene frame callback threw; skipping this overlay frame.");
                    }
                    SetStage(HookStage::EndFrame);
                    DX12Init::EndExternalFrame();
                }

                D3D12_RESOURCE_BARRIER toPresent = {};
                toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toPresent.Transition.pResource = frame.Backbuffer;
                toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                s_CommandList->ResourceBarrier(1, &toPresent);

                if (SUCCEEDED(s_CommandList->Close())) {
                    ID3D12CommandList* lists[] = { s_CommandList };
                    if (originalExecuteCommandLists != nullptr) {
                        originalExecuteCommandLists(s_CommandQueue, 1, lists);
                    } else {
                        s_CommandQueue->ExecuteCommandLists(1, lists);
                    }
                    SignalFrameFence(frame);
                }

                return originalPresent(swapChain, syncInterval, flags);
            } catch (const std::exception& ex) {
                LogHookMessage(ex.what());
                HandleOverlayFatal("HookPresent std::exception", E_FAIL);
                return originalPresent ? originalPresent(swapChain, syncInterval, flags) : S_OK;
            } catch (...) {
                HandleOverlayFatal("HookPresent unknown exception", E_FAIL);
                return originalPresent ? originalPresent(swapChain, syncInterval, flags) : S_OK;
            }
        }

        HRESULT HookResizeBuffers(
            IDXGISwapChain* swapChain,
            UINT bufferCount,
            UINT width,
            UINT height,
            DXGI_FORMAT format,
            UINT swapChainFlags,
            ExternalOverlayRuntime::ResizeBuffersFn originalResizeBuffers) {
            SetStage(HookStage::Resize);
            try {
                HandleResizeReset();
                if (originalResizeBuffers == nullptr) {
                    return E_FAIL;
                }
                return originalResizeBuffers(swapChain, bufferCount, width, height, format, swapChainFlags);
            } catch (...) {
                HandleOverlayFatal("HookResizeBuffers exception", E_FAIL);
                return originalResizeBuffers
                    ? originalResizeBuffers(swapChain, bufferCount, width, height, format, swapChainFlags)
                    : E_FAIL;
            }
        }

        HRESULT HookResizeBuffers1(
            IDXGISwapChain3* swapChain,
            UINT bufferCount,
            UINT width,
            UINT height,
            DXGI_FORMAT format,
            UINT swapChainFlags,
            const UINT* creationNodeMask,
            IUnknown* const* presentQueue,
            ExternalOverlayRuntime::ResizeBuffers1Fn originalResizeBuffers1) {
            SetStage(HookStage::Resize);
            try {
                HandleResizeReset();
                if (originalResizeBuffers1 == nullptr) {
                    return E_FAIL;
                }
                return originalResizeBuffers1(
                    swapChain,
                    bufferCount,
                    width,
                    height,
                    format,
                    swapChainFlags,
                    creationNodeMask,
                    presentQueue);
            } catch (...) {
                HandleOverlayFatal("HookResizeBuffers1 exception", E_FAIL);
                return originalResizeBuffers1
                    ? originalResizeBuffers1(
                        swapChain,
                        bufferCount,
                        width,
                        height,
                        format,
                        swapChainFlags,
                        creationNodeMask,
                        presentQueue)
                    : E_FAIL;
            }
        }
    } // namespace

    void ExternalOverlayRuntime::Configure(const Config& config) {
        s_RuntimeConfig = config;
        s_RuntimeInstance = this;
        s_ShuttingDown.store(false);
        SetStage(HookStage::Init);
        SetLastResult(S_OK);
        ResetQueueCaptureState();
    }

    void ExternalOverlayRuntime::CaptureQueueCandidate(ID3D12CommandQueue* commandQueue) {
        try {
            SetStage(HookStage::QueueCapture);
            if (!s_ShuttingDown.load() && commandQueue != nullptr) {
                CaptureQueueCandidateInternal(commandQueue);
            }
        } catch (...) {
            LogHookMessage("Queue capture failed; continuing with original command submission.");
        }
    }

    HRESULT ExternalOverlayRuntime::OnPresent(
        IDXGISwapChain* swapChain,
        UINT syncInterval,
        UINT flags,
        PresentFn originalPresent,
        ExecuteCommandListsFn originalExecuteCommandLists) {
        return HookPresent(swapChain, syncInterval, flags, originalPresent, originalExecuteCommandLists);
    }

    HRESULT ExternalOverlayRuntime::OnResizeBuffers(
        IDXGISwapChain* swapChain,
        UINT bufferCount,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        UINT swapChainFlags,
        ResizeBuffersFn originalResizeBuffers) {
        return HookResizeBuffers(
            swapChain,
            bufferCount,
            width,
            height,
            format,
            swapChainFlags,
            originalResizeBuffers);
    }

    HRESULT ExternalOverlayRuntime::OnResizeBuffers1(
        IDXGISwapChain3* swapChain,
        UINT bufferCount,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        UINT swapChainFlags,
        const UINT* creationNodeMask,
        IUnknown* const* presentQueue,
        ResizeBuffers1Fn originalResizeBuffers1) {
        return HookResizeBuffers1(
            swapChain,
            bufferCount,
            width,
            height,
            format,
            swapChainFlags,
            creationNodeMask,
            presentQueue,
            originalResizeBuffers1);
    }

        LRESULT ExternalOverlayRuntime::OnWndProc(
            HWND hWnd,
            UINT message,
            WPARAM wParam,
            LPARAM lParam,
            WNDPROC,
            bool& handled) {
        handled = false;
        try {
            if (!s_ShuttingDown.load()) {
                std::lock_guard<std::mutex> lock(s_InputQueueMutex);
                s_InputQueue.Push(QueuedMessage{ hWnd, message, wParam, lParam });

                auto updateMousePos = [&](bool nonClientCoords) {
                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    if (nonClientCoords) {
                        if (!ScreenToClient(hWnd, &pt)) {
                            return;
                        }
                    }
                    if (s_Window != nullptr && hWnd != nullptr && hWnd != s_Window) {
                        MapWindowPoints(hWnd, s_Window, &pt, 1);
                    }
                    s_RawInputState.MousePosClient = pt;
                    s_RawInputState.HasMousePos = true;
                };

                switch (message) {
                case WM_MOUSEMOVE:
                    updateMousePos(false);
                    break;
                case WM_NCMOUSEMOVE:
                    updateMousePos(true);
                    break;
                case WM_LBUTTONDOWN:
                case WM_LBUTTONDBLCLK:
                    updateMousePos(false);
                    s_RawInputState.MouseButtons[0] = true;
                    break;
                case WM_LBUTTONUP:
                    updateMousePos(false);
                    s_RawInputState.MouseButtons[0] = false;
                    break;
                case WM_RBUTTONDOWN:
                case WM_RBUTTONDBLCLK:
                    updateMousePos(false);
                    s_RawInputState.MouseButtons[1] = true;
                    break;
                case WM_RBUTTONUP:
                    updateMousePos(false);
                    s_RawInputState.MouseButtons[1] = false;
                    break;
                case WM_MBUTTONDOWN:
                case WM_MBUTTONDBLCLK:
                    updateMousePos(false);
                    s_RawInputState.MouseButtons[2] = true;
                    break;
                case WM_MBUTTONUP:
                    updateMousePos(false);
                    s_RawInputState.MouseButtons[2] = false;
                    break;
                case WM_XBUTTONDOWN:
                case WM_XBUTTONDBLCLK: {
                    updateMousePos(false);
                    const WORD xButton = GET_XBUTTON_WPARAM(wParam);
                    if (xButton == XBUTTON1) {
                        s_RawInputState.MouseButtons[3] = true;
                    } else if (xButton == XBUTTON2) {
                        s_RawInputState.MouseButtons[4] = true;
                    }
                    break;
                }
                case WM_XBUTTONUP: {
                    updateMousePos(false);
                    const WORD xButton = GET_XBUTTON_WPARAM(wParam);
                    if (xButton == XBUTTON1) {
                        s_RawInputState.MouseButtons[3] = false;
                    } else if (xButton == XBUTTON2) {
                        s_RawInputState.MouseButtons[4] = false;
                    }
                    break;
                }
                case WM_MOUSEWHEEL:
                    s_RawInputState.WheelY += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
                    break;
                case WM_MOUSEHWHEEL:
                    s_RawInputState.WheelX -= static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
                    break;
                default:
                    break;
                }
            }
            if (s_RuntimeConfig.CaptureHostInputAlways && s_Ready.load() && IsAlwaysCapturedInputMessage(message)) {
                handled = true;
                return 1;
            }
        } catch (...) {
            handled = false;
        }
        return 0;
    }

    void ExternalOverlayRuntime::Shutdown() {
        DoFullShutdown();
        s_ShuttingDown.store(false);
        s_RuntimeInstance = nullptr;
        s_RuntimeConfig = ExternalOverlayRuntime::Config{};
        SetStage(HookStage::None);
    }

    bool ExternalOverlayRuntime::IsReady() const {
        return s_Ready.load();
    }
}
