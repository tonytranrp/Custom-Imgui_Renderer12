#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "imgui.h"

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
