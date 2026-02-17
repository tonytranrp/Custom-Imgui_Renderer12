#include "DirectX12HookRuntime.hpp"

#include <Windows.h>

#include <atomic>
#include <string>

#include "Dx12Init/ExternalOverlayRuntime.hpp"
#include "Scene/ShowcaseRuntime.hpp"
#include "kiero.hpp"

namespace TestInjectedDx12::HookRuntime {
    namespace {
        using PresentFn = DX12Init::ExternalOverlayRuntime::PresentFn;
        using ExecuteCommandListsFn = DX12Init::ExternalOverlayRuntime::ExecuteCommandListsFn;
        using ResizeBuffersFn = DX12Init::ExternalOverlayRuntime::ResizeBuffersFn;
        using ResizeBuffers1Fn = DX12Init::ExternalOverlayRuntime::ResizeBuffers1Fn;

        std::atomic<bool> s_HooksInstalled = false;

        PresentFn s_OriginalPresent = nullptr;
        ExecuteCommandListsFn s_OriginalExecuteCommandLists = nullptr;
        ResizeBuffersFn s_OriginalResizeBuffers = nullptr;
        ResizeBuffers1Fn s_OriginalResizeBuffers1 = nullptr;

        DX12Init::ExternalOverlayRuntime s_OverlayRuntime;
        Scene::ShowcaseRuntime s_ShowcaseRuntime;

        void LogHookMessage(const char* message) {
            if (message == nullptr) {
                return;
            }
            OutputDebugStringA("[ImGuiDX12TestHook] ");
            OutputDebugStringA(message);
            OutputDebugStringA("\n");
        }

        bool HasConflictingOverlayModule(std::wstring& outModuleName) {
            static constexpr const wchar_t* kConflictModules[] = {
                L"graphics-hook64.dll",
                L"RTSSHooks64.dll",
                L"GameOverlayRenderer64.dll",
                L"DiscordHook64.dll",
                L"NvCameraAllowlisting64.dll"
            };

            for (const wchar_t* moduleName : kConflictModules) {
                if (moduleName == nullptr) {
                    continue;
                }
                if (GetModuleHandleW(moduleName) != nullptr) {
                    outModuleName = moduleName;
                    return true;
                }
            }
            return false;
        }

        void __stdcall HookExecuteCommandLists(
            ID3D12CommandQueue* commandQueue,
            UINT numCommandLists,
            ID3D12CommandList* const* commandLists) {
            s_OverlayRuntime.CaptureQueueCandidate(commandQueue);
            if (s_OriginalExecuteCommandLists != nullptr) {
                s_OriginalExecuteCommandLists(commandQueue, numCommandLists, commandLists);
            }
        }

        HRESULT __stdcall HookPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
            return s_OverlayRuntime.OnPresent(
                swapChain,
                syncInterval,
                flags,
                s_OriginalPresent,
                s_OriginalExecuteCommandLists);
        }

        HRESULT __stdcall HookResizeBuffers(
            IDXGISwapChain* swapChain,
            UINT bufferCount,
            UINT width,
            UINT height,
            DXGI_FORMAT format,
            UINT swapChainFlags) {
            return s_OverlayRuntime.OnResizeBuffers(
                swapChain,
                bufferCount,
                width,
                height,
                format,
                swapChainFlags,
                s_OriginalResizeBuffers);
        }

        HRESULT __stdcall HookResizeBuffers1(
            IDXGISwapChain3* swapChain,
            UINT bufferCount,
            UINT width,
            UINT height,
            DXGI_FORMAT format,
            UINT swapChainFlags,
            const UINT* creationNodeMask,
            IUnknown* const* presentQueue) {
            return s_OverlayRuntime.OnResizeBuffers1(
                swapChain,
                bufferCount,
                width,
                height,
                format,
                swapChainFlags,
                creationNodeMask,
                presentQueue,
                s_OriginalResizeBuffers1);
        }
    }

    bool Start() {
        if (s_HooksInstalled.load()) {
            return true;
        }

        Scene::ShowcaseRuntime::Options sceneOptions;
        sceneOptions.EnableImageLoading = true;
        sceneOptions.EnableShaderLoading = false;
        sceneOptions.ForceImmediateStartup = true;
        sceneOptions.RelaxRequiredFonts = true;
        sceneOptions.RelaxRequiredImages = true;
        sceneOptions.StripLocalPathMediaSources = true;
        sceneOptions.PreferRemoteBodyFont = true;
        s_ShowcaseRuntime.SetOptions(sceneOptions);

        DX12Init::ExternalOverlayRuntime::Config config;
        config.CaptureHostInputAlways = true;
        config.EnableRawInputFallback = true;
        config.AutoInitImGui = true;
        config.AutoInitShaderSystem = false;
        config.AllowFontAtlasRebuild = true;
        config.AllowShaderSystem = false;
        config.OnSceneSetup = [](HWND hWnd) {
            s_ShowcaseRuntime.Setup(hWnd);
        };
        config.OnSceneFrame = [](const DX12Init::FramePacket& frame) {
            s_ShowcaseRuntime.RenderFrame(frame);
        };
        config.OnSceneShutdown = []() {
            s_ShowcaseRuntime.Shutdown();
        };
        s_OverlayRuntime.Configure(config);

        std::wstring conflictModule;
        if (HasConflictingOverlayModule(conflictModule)) {
            LogHookMessage("Detected conflicting overlay module; injected hook disabled.");
            if (!conflictModule.empty()) {
                std::wstring detail = L"[ImGuiDX12TestHook] conflicting module: " + conflictModule + L"\n";
                OutputDebugStringW(detail.c_str());
            }
            return false;
        }

        const kiero::Status initStatus = kiero::init(kiero::RenderType::D3D12);
        if (initStatus != kiero::Status::Success) {
            return false;
        }

        if (kiero::bind<&IDXGISwapChain::Present>(&s_OriginalPresent, &HookPresent) != kiero::Status::Success ||
            kiero::bind<&ID3D12CommandQueue::ExecuteCommandLists>(&s_OriginalExecuteCommandLists, &HookExecuteCommandLists) != kiero::Status::Success ||
            kiero::bind<&IDXGISwapChain::ResizeBuffers>(&s_OriginalResizeBuffers, &HookResizeBuffers) != kiero::Status::Success ||
            kiero::bind<&IDXGISwapChain3::ResizeBuffers1>(&s_OriginalResizeBuffers1, &HookResizeBuffers1) != kiero::Status::Success) {
            kiero::shutdown();
            s_OriginalPresent = nullptr;
            s_OriginalExecuteCommandLists = nullptr;
            s_OriginalResizeBuffers = nullptr;
            s_OriginalResizeBuffers1 = nullptr;
            return false;
        }

        s_HooksInstalled.store(true);
        return true;
    }

    void Stop() {
        if (!s_HooksInstalled.load()) {
            return;
        }

        s_OverlayRuntime.Shutdown();
        kiero::shutdown();

        s_OriginalPresent = nullptr;
        s_OriginalExecuteCommandLists = nullptr;
        s_OriginalResizeBuffers = nullptr;
        s_OriginalResizeBuffers1 = nullptr;

        s_HooksInstalled.store(false);
    }

    bool IsReady() {
        return s_OverlayRuntime.IsReady();
    }
}
