#pragma once
#include <algorithm>
#include <vector>

#include "imgui.h"

namespace RenderUtils {
    struct GlowComponent {
        bool Enabled = true;
        ImU32 Color = IM_COL32(255, 255, 255, 255);
        float Radius = 10.0f; // How far the glow extends
        float Intensity = 1.0f; // Alpha multiplier (0.0 to 1.0+)
        
        // Configuration
        int Samples = 10; // Number of layers for the soft glow effect (Performance cost!)
        bool CacheEnabled = true;
        int MaxSamples = 16;
        mutable bool CacheDirty = true;
        mutable int CachedSampleCount = 0;
        mutable std::vector<float> CachedAlphaFactors;

        GlowComponent(ImU32 color = IM_COL32(255, 255, 255, 255), float radius = 10.0f, float intensity = 1.0f)
            : Color(color), Radius(radius), Intensity(intensity) {}

        GlowComponent& SetColor(ImU32 col) { Color = col; CacheDirty = true; return *this; }
        GlowComponent& SetRadius(float r) { Radius = r; CacheDirty = true; return *this; }
        GlowComponent& SetIntensity(float i) { Intensity = i; CacheDirty = true; return *this; }
        GlowComponent& SetSamples(int s) { Samples = s; CacheDirty = true; return *this; }
        GlowComponent& SetCacheEnabled(bool enabled) { CacheEnabled = enabled; CacheDirty = true; return *this; }
        GlowComponent& SetMaxSamples(int maxSamples) { MaxSamples = maxSamples; CacheDirty = true; return *this; }
        GlowComponent& SetEnabled(bool e) { Enabled = e; return *this; }

        int GetEffectiveSampleCount() const {
            const int hardMax = (std::max)(1, MaxSamples);
            return (std::max)(1, (std::min)(Samples, hardMax));
        }

        const std::vector<float>& GetAlphaFactors() const {
            const int effectiveSamples = GetEffectiveSampleCount();
            if (!CacheEnabled) {
                CachedAlphaFactors.resize(static_cast<size_t>(effectiveSamples));
                for (int i = 0; i < effectiveSamples; ++i) {
                    CachedAlphaFactors[static_cast<size_t>(i)] = 1.0f - (static_cast<float>(i) / static_cast<float>(effectiveSamples));
                }
                CachedSampleCount = effectiveSamples;
                return CachedAlphaFactors;
            }

            if (!CacheDirty && CachedSampleCount == effectiveSamples && CachedAlphaFactors.size() == static_cast<size_t>(effectiveSamples)) {
                return CachedAlphaFactors;
            }

            CachedAlphaFactors.resize(static_cast<size_t>(effectiveSamples));
            for (int i = 0; i < effectiveSamples; ++i) {
                CachedAlphaFactors[static_cast<size_t>(i)] = 1.0f - (static_cast<float>(i) / static_cast<float>(effectiveSamples));
            }
            CachedSampleCount = effectiveSamples;
            CacheDirty = false;
            return CachedAlphaFactors;
        }
    };
}
