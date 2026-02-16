#pragma once
#include "imgui.h"

namespace RenderUtils {
    // Z-Order Layering
    enum class ZOrder : int {
        Background = -10,
        Normal = 0,
        Above = 1,
        Below = -1,
        Top = 10,
        Overlay = 100
    };

    // Color Presets to avoid magic numbers
    enum class ColorPreset : ImU32 {
        Transparent = 0x00000000,
        White = 0xFFFFFFFF,
        Black = 0xFF000000,
        Red = 0xFF0000FF, // ABGR format often used in ImGui internally or IM_COL32
        Green = 0xFF00FF00,
        Blue = 0xFFFF0000,
        DarkBackground = 0xF0323232, // Dark Grey
        WindowBorder = 0xFF5A5050   // Greyish
    };
    
    // Helper to allow passing either raw ImU32 or ColorPreset
    // For now, we stick to ImU32 in struct but provide constants.

    // Styling properties
    struct StyleComponent {
        ImU32 BackgroundColor;
        ImU32 BorderColor;
        float BorderSize;
        float Rounding;
        ImDrawFlags RoundingFlags;
        bool  Visible;
        ZOrder Layer; // Renamed from ZIndex and changed type
        int ZIndexInt; // Cached int value for sorting if needed, or just cast Layer
        bool CalculatedVisible; // Internal: Is this entity effectively visible (considering parents)?
        bool UseGradient;
        ImU32 GradientTopColor;
        ImU32 GradientBottomColor;
        bool OutlineEnabled;
        ImU32 OutlineColor;
        float OutlineThickness;
        float ContentPaddingX;
        float ContentPaddingY;

        StyleComponent(ImU32 bg = 0, ImU32 border = 0, float borderSize = 0.0f, float rounding = 0.0f, bool visible = true, ZOrder layer = ZOrder::Normal, ImDrawFlags roundingFlags = ImDrawFlags_RoundCornersAll)
            : BackgroundColor(bg),
              BorderColor(border),
              BorderSize(borderSize),
              Rounding(rounding),
              RoundingFlags(roundingFlags),
              Visible(visible),
              Layer(layer),
              ZIndexInt(static_cast<int>(layer)),
              CalculatedVisible(visible),
              UseGradient(false),
              GradientTopColor(bg),
              GradientBottomColor(bg),
              OutlineEnabled(false),
              OutlineColor(IM_COL32(255, 255, 255, 0)),
              OutlineThickness(1.0f),
              ContentPaddingX(10.0f),
              ContentPaddingY(10.0f) {}

        StyleComponent& SetBackgroundColor(ImU32 color) { BackgroundColor = color; return *this; }
        StyleComponent& SetBorderColor(ImU32 color) { BorderColor = color; return *this; }
        StyleComponent& SetBorderSize(float size) { BorderSize = size; return *this; }
        StyleComponent& SetRounding(float rounding) { Rounding = rounding; return *this; }
        StyleComponent& SetRoundingFlags(ImDrawFlags flags) { RoundingFlags = flags; return *this; }
        StyleComponent& SetVisible(bool visible) { Visible = visible; return *this; }
        StyleComponent& SetLayer(ZOrder layer) { 
            Layer = layer; 
            ZIndexInt = static_cast<int>(layer); 
            return *this; 
        }
        StyleComponent& SetGradient(bool enabled, ImU32 topColor = 0, ImU32 bottomColor = 0) {
            UseGradient = enabled;
            if (topColor != 0) {
                GradientTopColor = topColor;
            } else {
                GradientTopColor = BackgroundColor;
            }
            if (bottomColor != 0) {
                GradientBottomColor = bottomColor;
            } else {
                GradientBottomColor = BackgroundColor;
            }
            return *this;
        }
        StyleComponent& SetGradientTopColor(ImU32 color) { GradientTopColor = color; return *this; }
        StyleComponent& SetGradientBottomColor(ImU32 color) { GradientBottomColor = color; return *this; }
        StyleComponent& SetOutline(bool enabled, ImU32 color = 0, float thickness = 1.0f) {
            OutlineEnabled = enabled;
            if (color != 0) {
                OutlineColor = color;
            }
            OutlineThickness = thickness;
            return *this;
        }
        StyleComponent& SetOutlineColor(ImU32 color) { OutlineColor = color; return *this; }
        StyleComponent& SetOutlineThickness(float thickness) { OutlineThickness = thickness; return *this; }
        StyleComponent& SetContentPadding(float x, float y) {
            ContentPaddingX = x;
            ContentPaddingY = y;
            return *this;
        }
    };
}
