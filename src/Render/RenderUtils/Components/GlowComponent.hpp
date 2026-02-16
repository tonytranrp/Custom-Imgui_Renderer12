#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "imgui.h"
#include "ShaderComponent.hpp"

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
        std::vector<ShaderParameter> ShaderParameters;
        bool ClipToParent = false;
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
        GlowComponent& SetClipToParent(bool clip) { ClipToParent = clip; return *this; }
        GlowComponent& SetQualityMode(GlowQualityMode quality) { QualityMode = quality; MarkCacheDirty(); return *this; }
        GlowComponent& SetFalloff(float falloff) { Falloff = falloff; MarkCacheDirty(); return *this; }
        GlowComponent& SetCoreStrength(float strength) { CoreStrength = strength; MarkCacheDirty(); return *this; }
        GlowComponent& SetInnerGlow(bool inner) { InnerGlow = inner; MarkCacheDirty(); return *this; }
        GlowComponent& SetOuterOnly(bool outerOnly) { OuterOnly = outerOnly; MarkCacheDirty(); return *this; }
        GlowComponent& SetRadiusScale(float scale) { RadiusScale = scale; MarkCacheDirty(); return *this; }
        GlowComponent& SetShaderParameters(const std::vector<ShaderParameter>& params) {
            ShaderParameters = params;
            return *this;
        }
        GlowComponent& ConfigureShaderParameters(const ShaderParamBuilder& builder) {
            ShaderParameters = builder.Parameters();
            return *this;
        }
        GlowComponent& ClearShaderParameters() {
            ShaderParameters.clear();
            return *this;
        }
        GlowComponent& ParamFloat(const std::string& name, float value) {
            UpsertShaderParameter(name, ShaderParamType::Float, value, ShaderBindingMode::Literal, {}, ShaderAutoUniform::None);
            return *this;
        }
        GlowComponent& ParamInt(const std::string& name, int value) {
            UpsertShaderParameter(name, ShaderParamType::Int, value, ShaderBindingMode::Literal, {}, ShaderAutoUniform::None);
            return *this;
        }
        GlowComponent& ParamVec2(const std::string& name, const ImVec2& value) {
            UpsertShaderParameter(name, ShaderParamType::Vec2, value, ShaderBindingMode::Literal, {}, ShaderAutoUniform::None);
            return *this;
        }
        GlowComponent& ParamVec4(const std::string& name, const ImVec4& value) {
            UpsertShaderParameter(name, ShaderParamType::Vec4, value, ShaderBindingMode::Literal, {}, ShaderAutoUniform::None);
            return *this;
        }
        GlowComponent& ParamColor(const std::string& name, ImU32 value) {
            UpsertShaderParameter(name, ShaderParamType::Color, value, ShaderBindingMode::Literal, {}, ShaderAutoUniform::None);
            return *this;
        }
        GlowComponent& ParamBool(const std::string& name, bool value) {
            UpsertShaderParameter(name, ShaderParamType::Bool, value, ShaderBindingMode::Literal, {}, ShaderAutoUniform::None);
            return *this;
        }
        GlowComponent& ParamAutoFloat(const std::string& name, ShaderAutoUniform binding, float fallback = 0.0f) {
            UpsertShaderParameter(name, ShaderParamType::Float, fallback, ShaderBindingMode::BuiltinAutoUniform, {}, binding);
            return *this;
        }
        GlowComponent& ParamAutoVec2(const std::string& name, ShaderAutoUniform binding, const ImVec2& fallback = ImVec2(0.0f, 0.0f)) {
            UpsertShaderParameter(name, ShaderParamType::Vec2, fallback, ShaderBindingMode::BuiltinAutoUniform, {}, binding);
            return *this;
        }
        GlowComponent& ParamAutoVec4(const std::string& name, ShaderAutoUniform binding, const ImVec4& fallback = ImVec4(0.0f, 0.0f, 0.0f, 0.0f)) {
            UpsertShaderParameter(name, ShaderParamType::Vec4, fallback, ShaderBindingMode::BuiltinAutoUniform, {}, binding);
            return *this;
        }
        GlowComponent& ParamBindFloat(const std::string& name, const std::string& uniformKey, float fallback = 0.0f) {
            UpsertShaderParameter(name, ShaderParamType::Float, fallback, ShaderBindingMode::RegisteredAutoUniform, uniformKey, ShaderAutoUniform::None);
            return *this;
        }
        GlowComponent& ParamBindInt(const std::string& name, const std::string& uniformKey, int fallback = 0) {
            UpsertShaderParameter(name, ShaderParamType::Int, fallback, ShaderBindingMode::RegisteredAutoUniform, uniformKey, ShaderAutoUniform::None);
            return *this;
        }
        GlowComponent& ParamBindVec2(const std::string& name, const std::string& uniformKey, const ImVec2& fallback = ImVec2(0.0f, 0.0f)) {
            UpsertShaderParameter(name, ShaderParamType::Vec2, fallback, ShaderBindingMode::RegisteredAutoUniform, uniformKey, ShaderAutoUniform::None);
            return *this;
        }
        GlowComponent& ParamBindVec4(const std::string& name, const std::string& uniformKey, const ImVec4& fallback = ImVec4(0.0f, 0.0f, 0.0f, 0.0f)) {
            UpsertShaderParameter(name, ShaderParamType::Vec4, fallback, ShaderBindingMode::RegisteredAutoUniform, uniformKey, ShaderAutoUniform::None);
            return *this;
        }
        GlowComponent& ParamBindColor(const std::string& name, const std::string& uniformKey, ImU32 fallback = IM_COL32(255, 255, 255, 255)) {
            UpsertShaderParameter(name, ShaderParamType::Color, fallback, ShaderBindingMode::RegisteredAutoUniform, uniformKey, ShaderAutoUniform::None);
            return *this;
        }
        GlowComponent& ParamBindBool(const std::string& name, const std::string& uniformKey, bool fallback = false) {
            UpsertShaderParameter(name, ShaderParamType::Bool, fallback, ShaderBindingMode::RegisteredAutoUniform, uniformKey, ShaderAutoUniform::None);
            return *this;
        }

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
        static std::string SanitizeParamName(const std::string& name) {
            if (name.empty()) {
                return "Param";
            }
            std::string out = name;
            for (size_t i = 0; i < out.size(); ++i) {
                const unsigned char ch = static_cast<unsigned char>(out[i]);
                const bool alphaNum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
                if (!(alphaNum || ch == '_')) {
                    out[i] = '_';
                }
            }
            const unsigned char first = static_cast<unsigned char>(out[0]);
            if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_')) {
                out.insert(out.begin(), '_');
            }
            return out;
        }

        void UpsertShaderParameter(
            const std::string& rawName,
            ShaderParamType type,
            const ShaderParamValue& value,
            ShaderBindingMode bindingMode,
            const std::string& uniformKey,
            ShaderAutoUniform autoUniform) {
            const std::string safeName = SanitizeParamName(rawName);
            for (auto& param : ShaderParameters) {
                if (param.Name == safeName) {
                    param.Type = type;
                    param.Value = value;
                    param.BindingMode = bindingMode;
                    param.UniformKey = uniformKey;
                    param.AutoUniform = autoUniform;
                    return;
                }
            }

            ShaderParameter param;
            param.Name = safeName;
            param.Type = type;
            param.Value = value;
            param.BindingMode = bindingMode;
            param.UniformKey = uniformKey;
            param.AutoUniform = autoUniform;
            ShaderParameters.push_back(std::move(param));
        }

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
