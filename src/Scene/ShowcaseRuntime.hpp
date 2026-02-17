#pragma once

#include <Windows.h>

#include "Dx12Init/Dx12Init.hpp"

namespace Scene {
    class ShowcaseRuntime {
    public:
        struct Options {
            bool EnableImageLoading = true;
            bool EnableShaderLoading = true;
            bool ForceImmediateStartup = false;
            bool RelaxRequiredFonts = false;
            bool RelaxRequiredImages = false;
            bool StripLocalPathMediaSources = false;
            bool PreferRemoteBodyFont = false;
        };

        void SetOptions(const Options& options);
        void Setup(HWND hWnd);
        void RenderFrame(const DX12Init::FramePacket& frame);
        void Shutdown();
    };
}
