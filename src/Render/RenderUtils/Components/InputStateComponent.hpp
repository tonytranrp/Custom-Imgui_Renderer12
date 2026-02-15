#pragma once

namespace RenderUtils {
    struct InputStateComponent {
        bool IsHovered;
        bool IsClicked;
        bool BlockInput; // If true, this entity consumes input and prevents it from passing through

        InputStateComponent(bool hovered = false, bool clicked = false, bool block = true)
            : IsHovered(hovered), IsClicked(clicked), BlockInput(block) {}

        InputStateComponent& SetHovered(bool hovered) { IsHovered = hovered; return *this; }
        InputStateComponent& SetClicked(bool clicked) { IsClicked = clicked; return *this; }
        InputStateComponent& SetBlockInput(bool block) { BlockInput = block; return *this; }
    };
}
