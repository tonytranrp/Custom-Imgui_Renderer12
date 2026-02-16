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

        static void Init(entt::registry& registry);

        static void FreeImageResource(entt::registry& registry, entt::entity entity);

        // Process Input (Dragging, Hovering) - Handles Z-Order Blocking
        static void UpdateInput(entt::registry& registry);

        // Update Text Input (Processing Characters)
        static void UpdateText(entt::registry& registry);

        // Update Animations
        static void UpdateAnimations(entt::registry& registry, float deltaTime);

        // Update all library systems (Input, Tabs, Animations, Layout, Transforms, etc.)
        static void Update(entt::registry& registry, float deltaTime);

        // Resolve dynamic Z-Order dependencies (DrawAboveComponent)
        static void ResolveDepth(entt::registry& registry);

        // Set Current Tab ID (for Tab Switching logic)
        static void SetCurrentTab(int tabId);
        static int GetCurrentTab();

        // Render Inspector Panel (ImGui Window) for the selected entity
        static void RenderInspector(entt::registry& registry);

    public:
        // Update Transform Hierarchy (Calculate World Positions from Relative Offsets)
        static void ResolveTransforms(entt::registry& registry);

        // The main render function that iterates over the registry
        static void Render(entt::registry& registry);

        // Helper: Find entity by name
        static entt::entity FindEntityByName(entt::registry& registry, const char* name);

        // Helper: Find child entity by name (scoped search)
        static entt::entity FindChildByName(entt::registry& registry, entt::entity parent, const char* name);

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
                                                
    private:
        static int s_CurrentTabId;
        static entt::entity s_SelectedEntity; // For Debug Inspector
        static entt::entity s_HoveredDebugEntity;

        // Image Loader
        static void UpdateImageLoader(entt::registry& registry);
    };
}
