#pragma once
#include "imgui.h"

namespace RenderUtils {
    struct GlowComponent {
        bool Enabled = true;
        ImU32 Color = IM_COL32(255, 255, 255, 255);
        float Radius = 10.0f; // How far the glow extends
        float Intensity = 1.0f; // Alpha multiplier (0.0 to 1.0+)
        
        // Configuration
        int Samples = 10; // Number of layers for the soft glow effect (Performance cost!)

        GlowComponent(ImU32 color = IM_COL32(255, 255, 255, 255), float radius = 10.0f, float intensity = 1.0f)
            : Color(color), Radius(radius), Intensity(intensity) {}

        GlowComponent& SetColor(ImU32 col) { Color = col; return *this; }
        GlowComponent& SetRadius(float r) { Radius = r; return *this; }
        GlowComponent& SetIntensity(float i) { Intensity = i; return *this; }
        GlowComponent& SetSamples(int s) { Samples = s; return *this; }
        GlowComponent& SetEnabled(bool e) { Enabled = e; return *this; }
    };
}
