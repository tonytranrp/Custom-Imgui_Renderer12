#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "imgui.h"
#include "ShaderComponent.hpp"

namespace RenderUtils {
    enum class ShadowRenderMode {
        Cpu,
        Shader
    };

    struct ShadowComponent {
        bool Enabled = true;
        ImU32 Color = IM_COL32(0, 0, 0, 140);
        ImVec2 Offset = ImVec2(4.0f, 4.0f);
        float BlurRadius = 14.0f;
        float Spread = 0.0f;
        int Samples = 12;
        bool Inset = false;
        ShadowRenderMode RenderMode = ShadowRenderMode::Cpu;
        std::string ShaderKey = "shadow.default";
        std::vector<ShaderParameter> ShaderParameters;
        bool ClipToParent = true;

        ShadowComponent() = default;

        ShadowComponent& SetEnabled(bool enabled) {
            Enabled = enabled;
            return *this;
        }

        ShadowComponent& SetColor(ImU32 color) {
            Color = color;
            return *this;
        }

        ShadowComponent& SetOffset(const ImVec2& offset) {
            Offset = offset;
            return *this;
        }

        ShadowComponent& SetBlurRadius(float blurRadius) {
            BlurRadius = blurRadius;
            return *this;
        }

        ShadowComponent& SetSpread(float spread) {
            Spread = spread;
            return *this;
        }

        ShadowComponent& SetSamples(int samples) {
            Samples = samples;
            return *this;
        }

        ShadowComponent& SetInset(bool inset) {
            Inset = inset;
            return *this;
        }

        ShadowComponent& SetRenderMode(ShadowRenderMode mode) {
            RenderMode = mode;
            return *this;
        }

        ShadowComponent& SetShaderKey(const std::string& key) {
            ShaderKey = key;
            return *this;
        }

        ShadowComponent& SetClipToParent(bool clip) {
            ClipToParent = clip;
            return *this;
        }

        ShadowComponent& SetShaderParameters(const std::vector<ShaderParameter>& params) {
            ShaderParameters = params;
            return *this;
        }

        ShadowComponent& ConfigureShaderParameters(const ShaderParamBuilder& builder) {
            ShaderParameters = builder.Parameters();
            return *this;
        }

        ShadowComponent& ClearShaderParameters() {
            ShaderParameters.clear();
            return *this;
        }

        ShadowComponent& ParamFloat(const std::string& name, float value) {
            UpsertShaderParameter(name, ShaderParamType::Float, value, ShaderBindingMode::Literal, {}, ShaderAutoUniform::None);
            return *this;
        }

        ShadowComponent& ParamInt(const std::string& name, int value) {
            UpsertShaderParameter(name, ShaderParamType::Int, value, ShaderBindingMode::Literal, {}, ShaderAutoUniform::None);
            return *this;
        }

        ShadowComponent& ParamVec2(const std::string& name, const ImVec2& value) {
            UpsertShaderParameter(name, ShaderParamType::Vec2, value, ShaderBindingMode::Literal, {}, ShaderAutoUniform::None);
            return *this;
        }

        ShadowComponent& ParamVec4(const std::string& name, const ImVec4& value) {
            UpsertShaderParameter(name, ShaderParamType::Vec4, value, ShaderBindingMode::Literal, {}, ShaderAutoUniform::None);
            return *this;
        }

        ShadowComponent& ParamColor(const std::string& name, ImU32 value) {
            UpsertShaderParameter(name, ShaderParamType::Color, value, ShaderBindingMode::Literal, {}, ShaderAutoUniform::None);
            return *this;
        }

        ShadowComponent& ParamBool(const std::string& name, bool value) {
            UpsertShaderParameter(name, ShaderParamType::Bool, value, ShaderBindingMode::Literal, {}, ShaderAutoUniform::None);
            return *this;
        }

        ShadowComponent& ParamAutoFloat(const std::string& name, ShaderAutoUniform binding, float fallback = 0.0f) {
            UpsertShaderParameter(name, ShaderParamType::Float, fallback, ShaderBindingMode::BuiltinAutoUniform, {}, binding);
            return *this;
        }

        ShadowComponent& ParamAutoVec2(const std::string& name, ShaderAutoUniform binding, const ImVec2& fallback = ImVec2(0.0f, 0.0f)) {
            UpsertShaderParameter(name, ShaderParamType::Vec2, fallback, ShaderBindingMode::BuiltinAutoUniform, {}, binding);
            return *this;
        }

        ShadowComponent& ParamAutoVec4(const std::string& name, ShaderAutoUniform binding, const ImVec4& fallback = ImVec4(0.0f, 0.0f, 0.0f, 0.0f)) {
            UpsertShaderParameter(name, ShaderParamType::Vec4, fallback, ShaderBindingMode::BuiltinAutoUniform, {}, binding);
            return *this;
        }

        ShadowComponent& ParamBindFloat(const std::string& name, const std::string& uniformKey, float fallback = 0.0f) {
            UpsertShaderParameter(name, ShaderParamType::Float, fallback, ShaderBindingMode::RegisteredAutoUniform, uniformKey, ShaderAutoUniform::None);
            return *this;
        }

        ShadowComponent& ParamBindInt(const std::string& name, const std::string& uniformKey, int fallback = 0) {
            UpsertShaderParameter(name, ShaderParamType::Int, fallback, ShaderBindingMode::RegisteredAutoUniform, uniformKey, ShaderAutoUniform::None);
            return *this;
        }

        ShadowComponent& ParamBindVec2(const std::string& name, const std::string& uniformKey, const ImVec2& fallback = ImVec2(0.0f, 0.0f)) {
            UpsertShaderParameter(name, ShaderParamType::Vec2, fallback, ShaderBindingMode::RegisteredAutoUniform, uniformKey, ShaderAutoUniform::None);
            return *this;
        }

        ShadowComponent& ParamBindVec4(const std::string& name, const std::string& uniformKey, const ImVec4& fallback = ImVec4(0.0f, 0.0f, 0.0f, 0.0f)) {
            UpsertShaderParameter(name, ShaderParamType::Vec4, fallback, ShaderBindingMode::RegisteredAutoUniform, uniformKey, ShaderAutoUniform::None);
            return *this;
        }

        ShadowComponent& ParamBindColor(const std::string& name, const std::string& uniformKey, ImU32 fallback = IM_COL32(255, 255, 255, 255)) {
            UpsertShaderParameter(name, ShaderParamType::Color, fallback, ShaderBindingMode::RegisteredAutoUniform, uniformKey, ShaderAutoUniform::None);
            return *this;
        }

        ShadowComponent& ParamBindBool(const std::string& name, const std::string& uniformKey, bool fallback = false) {
            UpsertShaderParameter(name, ShaderParamType::Bool, fallback, ShaderBindingMode::RegisteredAutoUniform, uniformKey, ShaderAutoUniform::None);
            return *this;
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

    };

    namespace ShadowSystem {
        inline void Draw(const ShadowComponent& shadow, ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
            float rounding, ImDrawFlags roundingFlags, float alphaMultiplier = 1.0f) {
            if (!drawList || !shadow.Enabled) {
                return;
            }

            const int samples = (std::max)(1, (std::min)(64, shadow.Samples));
            const float blurRadius = (std::max)(0.0f, shadow.BlurRadius);
            if (blurRadius <= 0.0f) {
                return;
            }

            const float spread = shadow.Spread;
            const float clampedAlphaMul = (std::max)(0.0f, (std::min)(1.0f, alphaMultiplier));
            if (clampedAlphaMul <= 0.0f) {
                return;
            }

            const float baseAlpha = static_cast<float>((shadow.Color >> 24) & 0xFF) * clampedAlphaMul;
            if (baseAlpha <= 0.0f) {
                return;
            }
            const int colorR = (shadow.Color >> 0) & 0xFF;
            const int colorG = (shadow.Color >> 8) & 0xFF;
            const int colorB = (shadow.Color >> 16) & 0xFF;

            std::vector<float> weights(static_cast<size_t>(samples), 0.0f);
            float weightSum = 0.0f;
            for (int i = 0; i < samples; ++i) {
                const float t = static_cast<float>(i + 1) / static_cast<float>(samples);
                // Slightly center-heavy gaussian-like falloff so shadows look like area blur.
                const float w = std::exp(-(t * t) * 2.4f);
                weights[static_cast<size_t>(i)] = w;
                weightSum += w;
            }
            if (weightSum <= 0.0001f) {
                return;
            }
            const float invWeightSum = 1.0f / weightSum;

            const ImVec2 baseMin = ImVec2(pMin.x + shadow.Offset.x - spread, pMin.y + shadow.Offset.y - spread);
            const ImVec2 baseMax = ImVec2(pMax.x + shadow.Offset.x + spread, pMax.y + shadow.Offset.y + spread);
            const float safeRounding = (std::max)(0.0f, rounding);

            for (int i = samples - 1; i >= 0; --i) {
                const float t = static_cast<float>(i + 1) / static_cast<float>(samples);
                const float distance = blurRadius * t;
                const float normalizedWeight = weights[static_cast<size_t>(i)] * invWeightSum;
                const float layerAlpha = (std::max)(0.0f, (std::min)(255.0f, baseAlpha * normalizedWeight * 2.5f));
                if (layerAlpha <= 0.5f) {
                    continue;
                }

                const ImU32 color = IM_COL32(colorR, colorG, colorB, static_cast<int>(layerAlpha));
                if (shadow.Inset) {
                    ImVec2 insetMin = ImVec2(baseMin.x + distance, baseMin.y + distance);
                    ImVec2 insetMax = ImVec2(baseMax.x - distance, baseMax.y - distance);
                    if (insetMax.x <= insetMin.x || insetMax.y <= insetMin.y) {
                        continue;
                    }
                    drawList->AddRectFilled(
                        insetMin,
                        insetMax,
                        color,
                        (std::max)(0.0f, safeRounding - distance),
                        roundingFlags);
                } else {
                    drawList->AddRectFilled(
                        ImVec2(baseMin.x - distance, baseMin.y - distance),
                        ImVec2(baseMax.x + distance, baseMax.y + distance),
                        color,
                        safeRounding + distance,
                        roundingFlags);
                }
            }

            if (!shadow.Inset && spread > 0.0f) {
                const float coreAlpha = (std::max)(0.0f, (std::min)(255.0f, baseAlpha * 0.20f));
                if (coreAlpha > 0.5f) {
                    drawList->AddRectFilled(
                        baseMin,
                        baseMax,
                        IM_COL32(colorR, colorG, colorB, static_cast<int>(coreAlpha)),
                        safeRounding,
                        roundingFlags);
                }
            }
        }
    } // namespace ShadowSystem
} // namespace RenderUtils
