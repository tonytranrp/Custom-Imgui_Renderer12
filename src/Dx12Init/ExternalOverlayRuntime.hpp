#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <functional>

#include "Dx12Init/Dx12Init.hpp"

namespace DX12Init {
    class ExternalOverlayRuntime {
    public:
        using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
        using ExecuteCommandListsFn = void(__stdcall*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
        using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
        using ResizeBuffers1Fn = HRESULT(__stdcall*)(
            IDXGISwapChain3*,
            UINT,
            UINT,
            UINT,
            DXGI_FORMAT,
            UINT,
            const UINT*,
            IUnknown* const*);

        struct Config {
            std::function<void(HWND)> OnSceneSetup;
            std::function<void(const FramePacket&)> OnSceneFrame;
            std::function<void()> OnSceneShutdown;
            bool CaptureHostInputAlways = true;
            bool EnableRawInputFallback = true;
            bool AutoInitImGui = true;
            bool AutoInitShaderSystem = false;
            bool AllowFontAtlasRebuild = false;
            bool AllowShaderSystem = false;
        };

        ExternalOverlayRuntime() = default;

        void Configure(const Config& config);
        void CaptureQueueCandidate(ID3D12CommandQueue* commandQueue);
        HRESULT OnPresent(
            IDXGISwapChain* swapChain,
            UINT syncInterval,
            UINT flags,
            PresentFn originalPresent,
            ExecuteCommandListsFn originalExecuteCommandLists);
        HRESULT OnResizeBuffers(
            IDXGISwapChain* swapChain,
            UINT bufferCount,
            UINT width,
            UINT height,
            DXGI_FORMAT format,
            UINT swapChainFlags,
            ResizeBuffersFn originalResizeBuffers);
        HRESULT OnResizeBuffers1(
            IDXGISwapChain3* swapChain,
            UINT bufferCount,
            UINT width,
            UINT height,
            DXGI_FORMAT format,
            UINT swapChainFlags,
            const UINT* creationNodeMask,
            IUnknown* const* presentQueue,
            ResizeBuffers1Fn originalResizeBuffers1);
        LRESULT OnWndProc(
            HWND hWnd,
            UINT message,
            WPARAM wParam,
            LPARAM lParam,
            WNDPROC originalWndProc,
            bool& handled);
        void Shutdown();
        bool IsReady() const;
    };
}
