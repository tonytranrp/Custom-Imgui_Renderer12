#pragma once
#include <functional>
#include "imgui.h"
#include "entt/entt.hpp"

namespace RenderUtils {

    // Component that allows users to define custom rendering logic
    // while still benefiting from the framework's layout, collision, and input handling.
    struct CustomComponent {
        // Callback function for custom drawing.
        // Parameters:
        // - registry: Reference to the EnTT registry (allows accessing other components).
        // - entity: The entity being rendered.
        // - drawList: The ImDrawList to draw into.
        // - p_min: The top-left corner of the component's bounding box.
        // - p_max: The bottom-right corner of the component's bounding box.
        // - isHovered: Whether the mouse is currently hovering over this component.
        // - isClicked: Whether the component is currently being clicked (mouse down).
        using RenderCallback = std::function<void(entt::registry& registry, entt::entity entity, ImDrawList* drawList, ImVec2 p_min, ImVec2 p_max, bool isHovered, bool isClicked)>;

        RenderCallback OnRender;

        CustomComponent(RenderCallback callback = nullptr) : OnRender(callback) {}

        CustomComponent& SetOnRender(RenderCallback callback) {
            OnRender = callback;
            return *this;
        }
    };
}
