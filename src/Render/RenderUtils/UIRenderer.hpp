#pragma once

#include "entt/entt.hpp"
#include "imgui.h"
#include "UIComponents.hpp" // Added include for Component definitions

#include <vector>

namespace RenderUtils {

    class UIRenderer {
    public:
        // Global state for debug/interaction
        static bool DebugMode;

        static void Init();

        // Process Input (Dragging, Hovering) - Handles Z-Order Blocking
        static void UpdateInput(entt::registry& registry);

        // Resolve dynamic Z-Order dependencies (DrawAboveComponent)
        static void ResolveDepth(entt::registry& registry);

        // The main render function that iterates over the registry
        static void Render(entt::registry& registry);

        // Helper: Find entity by name
        static entt::entity FindEntityByName(entt::registry& registry, const char* name);

        // Helper: Calculate precise text bounds (lines)
        static std::vector<ImVec4> CalculateTextLines(const TextComponent& text, const TransformComponent& transform, const ContainerComponent* container = nullptr);

        // Helper to create a basic container entity
        static entt::entity CreateContainer(entt::registry& registry, const char* name, 
                                            const ImVec2& pos, const ImVec2& size, 
                                            ImU32 color = IM_COL32(50, 50, 50, 255));
        
        // Helper to create a child container
        static entt::entity CreateChildContainer(entt::registry& registry, entt::entity parent, const char* name,
                                                const ImVec2& relativePos, const ImVec2& size,
                                                ImU32 color = IM_COL32(70, 70, 80, 255));
    };
}
