#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "imgui.h"

namespace RenderUtils {
    enum class GlowMode {
        GaussianBloom,
        NeonTube,
        AmbientSoft
    };

    enum class GlowQualityMode {
        Performance,
        Balanced,
        Ultra
    };

    enum class GlowRenderMode {
        Cpu,
        Shader
    };

    struct GlowComponent {
        bool Enabled = true;
        ImU32 Color = IM_COL32(255, 255, 255, 255);
        float Radius = 10.0f; // How far the glow extends
        float Intensity = 1.0f; // Alpha multiplier (0.0 to 1.0+)
        GlowRenderMode RenderMode = GlowRenderMode::Shader;
        std::string ShaderKey = "glow.default";
        GlowMode Mode = GlowMode::GaussianBloom;
        GlowQualityMode QualityMode = GlowQualityMode::Ultra;
        float Falloff = 1.0f;
        float CoreStrength = 0.35f;
        bool InnerGlow = false;
        bool OuterOnly = false;
        float RadiusScale = 1.0f;
        
        // Configuration
        int Samples = 10; // Number of layers for the soft glow effect (Performance cost!)
        bool CacheEnabled = true;
        int MaxSamples = 16;
        mutable bool CacheDirty = true;
        mutable int CachedSampleCount = 0;
        mutable GlowMode CachedMode = GlowMode::GaussianBloom;
        mutable GlowQualityMode CachedQualityMode = GlowQualityMode::Ultra;
        mutable float CachedFalloff = 1.0f;
        mutable float CachedCoreStrength = 0.35f;
        mutable float CachedRadiusScale = 1.0f;
        mutable std::vector<float> CachedKernelDistances;
        mutable std::vector<float> CachedKernelWeights;

        GlowComponent(ImU32 color = IM_COL32(255, 255, 255, 255), float radius = 10.0f, float intensity = 1.0f)
            : Color(color), Radius(radius), Intensity(intensity) {}

        GlowComponent& SetColor(ImU32 col) { Color = col; MarkCacheDirty(); return *this; }
        GlowComponent& SetRadius(float r) { Radius = r; MarkCacheDirty(); return *this; }
        GlowComponent& SetIntensity(float i) { Intensity = i; return *this; }
        GlowComponent& SetSamples(int s) { Samples = s; MarkCacheDirty(); return *this; }
        GlowComponent& SetCacheEnabled(bool enabled) { CacheEnabled = enabled; MarkCacheDirty(); return *this; }
        GlowComponent& SetMaxSamples(int maxSamples) { MaxSamples = maxSamples; MarkCacheDirty(); return *this; }
        GlowComponent& SetEnabled(bool e) { Enabled = e; return *this; }
        GlowComponent& SetMode(GlowMode mode) { Mode = mode; MarkCacheDirty(); return *this; }
        GlowComponent& SetRenderMode(GlowRenderMode mode) { RenderMode = mode; return *this; }
        GlowComponent& SetShaderKey(const std::string& key) { ShaderKey = key; return *this; }
        GlowComponent& SetQualityMode(GlowQualityMode quality) { QualityMode = quality; MarkCacheDirty(); return *this; }
        GlowComponent& SetFalloff(float falloff) { Falloff = falloff; MarkCacheDirty(); return *this; }
        GlowComponent& SetCoreStrength(float strength) { CoreStrength = strength; MarkCacheDirty(); return *this; }
        GlowComponent& SetInnerGlow(bool inner) { InnerGlow = inner; MarkCacheDirty(); return *this; }
        GlowComponent& SetOuterOnly(bool outerOnly) { OuterOnly = outerOnly; MarkCacheDirty(); return *this; }
        GlowComponent& SetRadiusScale(float scale) { RadiusScale = scale; MarkCacheDirty(); return *this; }

        void MarkCacheDirty() { CacheDirty = true; }

        int GetEffectiveSampleCount() const {
            const int hardMax = (std::max)(1, (std::min)(MaxSamples, 96));
            int qualityCap = hardMax;
            if (QualityMode == GlowQualityMode::Performance) {
                qualityCap = (std::min)(qualityCap, 12);
            } else if (QualityMode == GlowQualityMode::Balanced) {
                qualityCap = (std::min)(qualityCap, 24);
            } else {
                qualityCap = (std::min)(qualityCap, 48);
            }
            const int target = (std::max)(1, (std::min)(Samples, hardMax));
            return (std::max)(1, (std::min)(target, qualityCap));
        }

        const std::vector<float>& GetKernelDistances() const {
            BuildKernelIfNeeded();
            return CachedKernelDistances;
        }

        const std::vector<float>& GetKernelWeights() const {
            BuildKernelIfNeeded();
            return CachedKernelWeights;
        }

        // Backward-compatible alias for existing renderer call-sites.
        const std::vector<float>& GetAlphaFactors() const {
            return GetKernelWeights();
        }

    private:
        static float EvaluateWeight(GlowMode mode, float t, float falloff, float coreStrength) {
            const float safeT = (std::max)(0.0f, (std::min)(1.0f, t));
            const float safeFalloff = (std::max)(0.2f, (std::min)(4.0f, falloff));
            const float safeCore = (std::max)(0.0f, (std::min)(2.0f, coreStrength));

            if (mode == GlowMode::NeonTube) {
                // Strong near-edge core with controlled outer tail.
                const float edge = std::pow(1.0f - safeT, 0.45f + (0.25f / safeFalloff));
                const float tail = std::exp(-std::pow(safeT * (1.6f + safeFalloff), 2.0f));
                return (0.65f * edge + 0.35f * tail) * (1.0f + safeCore * 0.55f);
            }

            if (mode == GlowMode::AmbientSoft) {
                // Wider, low-contrast ambient halo.
                const float ambient = std::exp(-safeT * (1.0f + safeFalloff * 0.9f));
                const float smooth = std::pow(1.0f - safeT, 1.25f + 0.35f * safeFalloff);
                return 0.5f * ambient + 0.5f * smooth;
            }

            // GaussianBloom default: smooth physically-inspired falloff.
            const float sigma = (std::max)(0.12f, 0.42f / safeFalloff);
            const float gaussian = std::exp(-(safeT * safeT) / (2.0f * sigma * sigma));
            return gaussian;
        }

        void BuildKernelIfNeeded() const {
            const int effectiveSamples = GetEffectiveSampleCount();
            const bool cacheValid = !CacheDirty &&
                                    CachedSampleCount == effectiveSamples &&
                                    CachedMode == Mode &&
                                    CachedQualityMode == QualityMode &&
                                    std::fabs(CachedFalloff - Falloff) <= 0.0001f &&
                                    std::fabs(CachedCoreStrength - CoreStrength) <= 0.0001f &&
                                    std::fabs(CachedRadiusScale - RadiusScale) <= 0.0001f &&
                                    CachedKernelWeights.size() == static_cast<size_t>(effectiveSamples) &&
                                    CachedKernelDistances.size() == static_cast<size_t>(effectiveSamples);

            if (CacheEnabled && cacheValid) {
                return;
            }

            CachedKernelWeights.resize(static_cast<size_t>(effectiveSamples));
            CachedKernelDistances.resize(static_cast<size_t>(effectiveSamples));
            const float safeRadiusScale = (std::max)(0.1f, (std::min)(3.0f, RadiusScale));
            float weightSum = 0.0f;
            for (int i = 0; i < effectiveSamples; ++i) {
                const float t = static_cast<float>(i + 1) / static_cast<float>(effectiveSamples);
                const float distance = std::pow(t, 0.9f) * safeRadiusScale;
                const float weight = EvaluateWeight(Mode, t, Falloff, CoreStrength);
                CachedKernelDistances[static_cast<size_t>(i)] = distance;
                CachedKernelWeights[static_cast<size_t>(i)] = (std::max)(0.0001f, weight);
                weightSum += CachedKernelWeights[static_cast<size_t>(i)];
            }

            if (weightSum <= 0.0001f) {
                const float uniform = 1.0f / static_cast<float>(effectiveSamples);
                for (int i = 0; i < effectiveSamples; ++i) {
                    CachedKernelWeights[static_cast<size_t>(i)] = uniform;
                }
            } else {
                const float invSum = 1.0f / weightSum;
                for (int i = 0; i < effectiveSamples; ++i) {
                    CachedKernelWeights[static_cast<size_t>(i)] *= invSum;
                }
            }

            CachedSampleCount = effectiveSamples;
            CachedMode = Mode;
            CachedQualityMode = QualityMode;
            CachedFalloff = Falloff;
            CachedCoreStrength = CoreStrength;
            CachedRadiusScale = RadiusScale;
            CacheDirty = false;
        }
    };
}
