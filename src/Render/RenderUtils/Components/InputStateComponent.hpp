#pragma once

#include "imgui.h"

namespace RenderUtils {
    struct InputStateComponent {
        bool IsHovered;
        bool IsClicked;
        bool BlockInput; // If true, this entity consumes input and prevents it from passing through
        ImVec4 ClipRect; // (MinX, MinY, MaxX, MaxY) - The visible region of this entity (calculated by ResolveTransforms)

        InputStateComponent(bool hovered = false, bool clicked = false, bool block = true)
            : IsHovered(hovered), IsClicked(clicked), BlockInput(block), ClipRect(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX) {}

        InputStateComponent& SetHovered(bool hovered) { IsHovered = hovered; return *this; }
        InputStateComponent& SetClicked(bool clicked) { IsClicked = clicked; return *this; }
        InputStateComponent& SetBlockInput(bool block) { BlockInput = block; return *this; }
    };
}
