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

        StyleComponent(ImU32 bg = 0, ImU32 border = 0, float borderSize = 0.0f, float rounding = 0.0f, bool visible = true, ZOrder layer = ZOrder::Normal, ImDrawFlags roundingFlags = ImDrawFlags_RoundCornersAll)
            : BackgroundColor(bg), BorderColor(border), BorderSize(borderSize), Rounding(rounding), Visible(visible), Layer(layer), ZIndexInt(static_cast<int>(layer)), RoundingFlags(roundingFlags) {}

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
    };
}
