#pragma once

#include <algorithm>

#include "imgui.h"

namespace RenderUtils {
    struct ShadowComponent {
        bool Enabled = true;
        ImU32 Color = IM_COL32(0, 0, 0, 140);
        ImVec2 Offset = ImVec2(4.0f, 4.0f);
        float BlurRadius = 14.0f;
        float Spread = 0.0f;
        int Samples = 12;
        bool Inset = false;

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
    };

    namespace ShadowSystem {
        inline void Draw(const ShadowComponent& shadow, ImDrawList* drawList, const ImVec2& pMin, const ImVec2& pMax,
            float rounding, ImDrawFlags roundingFlags) {
            if (!drawList || !shadow.Enabled) {
                return;
            }

            const int samples = (std::max)(1, (std::min)(32, shadow.Samples));
            const float blurRadius = (std::max)(0.0f, shadow.BlurRadius);
            if (blurRadius <= 0.0f) {
                return;
            }

            const float spread = shadow.Spread;
            const float baseAlpha = static_cast<float>((shadow.Color >> 24) & 0xFF);
            const int colorR = (shadow.Color >> 0) & 0xFF;
            const int colorG = (shadow.Color >> 8) & 0xFF;
            const int colorB = (shadow.Color >> 16) & 0xFF;

            for (int i = 0; i < samples; ++i) {
                const float t = static_cast<float>(i + 1) / static_cast<float>(samples);
                const float distance = spread + blurRadius * t;
                const float falloff = 1.0f - (static_cast<float>(i) / static_cast<float>(samples));
                const float alpha = (std::max)(0.0f, (std::min)(255.0f, baseAlpha * falloff / static_cast<float>(samples)));
                const ImU32 color = IM_COL32(colorR, colorG, colorB, static_cast<int>(alpha));

                if (shadow.Inset) {
                    drawList->AddRect(
                        ImVec2(pMin.x + shadow.Offset.x + distance, pMin.y + shadow.Offset.y + distance),
                        ImVec2(pMax.x + shadow.Offset.x - distance, pMax.y + shadow.Offset.y - distance),
                        color,
                        (std::max)(0.0f, rounding - distance),
                        roundingFlags,
                        1.0f);
                } else {
                    drawList->AddRect(
                        ImVec2(pMin.x + shadow.Offset.x - distance, pMin.y + shadow.Offset.y - distance),
                        ImVec2(pMax.x + shadow.Offset.x + distance, pMax.y + shadow.Offset.y + distance),
                        color,
                        rounding + distance,
                        roundingFlags,
                        1.0f);
                }
            }
        }
    } // namespace ShadowSystem
} // namespace RenderUtils
