#include "MainRendering.hpp"

#include "Dx12Init/Dx12Init.hpp"
#include "Scene/ShowcaseRuntime.hpp"

namespace MainRendering {
    int Run(HINSTANCE hInstance) {
        DX12Init::RunConfig runConfig;
        runConfig.Window.ClassName = L"ImGui DX12 Class";
        runConfig.Window.Title = L"ImGui DX12 App";
        runConfig.Window.PosX = 100;
        runConfig.Window.PosY = 100;
        runConfig.Window.Width = 1600;
        runConfig.Window.Height = 900;
        runConfig.Window.WindowStyle = WS_OVERLAPPEDWINDOW;
        runConfig.Window.ShowCmd = SW_SHOWDEFAULT;
        runConfig.VSync = true;
        runConfig.AutoInitImGui = true;
        runConfig.AutoInitShaderSystem = true;
        runConfig.AutoShowWindow = true;

        Scene::ShowcaseRuntime runtime;

        DX12Init::RuntimeCallbacks callbacks;
        callbacks.OnSetup = [&runtime](HWND hWnd) {
            runtime.Setup(hWnd);
        };
        callbacks.OnFrame = [&runtime](const DX12Init::FramePacket& frame) {
            runtime.RenderFrame(frame);
        };
        callbacks.OnShutdown = [&runtime]() {
            runtime.Shutdown();
        };

        return DX12Init::RunApp(hInstance, runConfig, callbacks);
    }
}
