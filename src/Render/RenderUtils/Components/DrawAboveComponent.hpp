#pragma once

namespace RenderUtils {
    // Component to define relative Z-ordering dependency
    struct DrawAboveComponent {
        const char* TargetEntityName; // Name of the entity this should be drawn above

        DrawAboveComponent(const char* targetName = "") : TargetEntityName(targetName) {}

        DrawAboveComponent& SetTargetEntityName(const char* name) { TargetEntityName = name; return *this; }
    };
}
