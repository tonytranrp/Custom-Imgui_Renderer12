#pragma once
#include <functional>
#include "imgui.h"
#include "entt/entt.hpp"
#include "../Components/InputStateComponent.hpp"

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
        // OnUpdate: frame-time logic that should run before rendering.
        using UpdateCallback = std::function<void(entt::registry& registry, entt::entity entity, float deltaTime)>;
        // OnInput: edge-triggered input handling (InputStateComponent::JustPressed / JustReleased).
        // Typical flow: react in OnInput and start/stop AnimationComponent timelines.
        using InputCallback = std::function<void(entt::registry& registry, entt::entity entity, const InputStateComponent& input)>;

        RenderCallback OnRender;
        UpdateCallback OnUpdate;
        InputCallback OnInput;
        bool Enabled = true;
        int Priority = 0;

        CustomComponent(RenderCallback callback = nullptr) : OnRender(callback) {}

        CustomComponent& SetOnRender(RenderCallback callback) {
            OnRender = callback;
            return *this;
        }

        CustomComponent& SetOnUpdate(UpdateCallback callback) {
            OnUpdate = callback;
            return *this;
        }

        CustomComponent& SetOnInput(InputCallback callback) {
            OnInput = callback;
            return *this;
        }

        CustomComponent& SetEnabled(bool enabled) {
            Enabled = enabled;
            return *this;
        }

        CustomComponent& SetPriority(int priority) {
            Priority = priority;
            return *this;
        }
    };
}
