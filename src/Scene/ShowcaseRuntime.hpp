#pragma once

#include <Windows.h>

#include "Dx12Init/Dx12Init.hpp"

namespace Scene {
    class ShowcaseRuntime {
    public:
        struct Options {
            bool EnableImageLoading = true;
            bool EnableShaderLoading = true;
        };

        void SetOptions(const Options& options);
        void Setup(HWND hWnd);
        void RenderFrame(const DX12Init::FramePacket& frame);
        void Shutdown();
    };
}
