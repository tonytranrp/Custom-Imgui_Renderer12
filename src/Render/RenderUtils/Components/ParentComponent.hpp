#pragma once
#include "imgui.h"
#include "entt/entt.hpp"

namespace RenderUtils {
    // Parent/Child Relationship
    struct ParentComponent {
        ImVec2 RelativeOffset; // Offset from parent position
        entt::entity ParentEntity = entt::null;

        ParentComponent& SetParent(entt::entity parent) { ParentEntity = parent; return *this; }
        ParentComponent& SetRelativeOffset(const ImVec2& offset) { RelativeOffset = offset; return *this; }
    };
}
