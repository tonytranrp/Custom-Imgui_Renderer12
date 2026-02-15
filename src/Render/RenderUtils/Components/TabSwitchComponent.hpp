#pragma once

namespace RenderUtils {
    // Component to define Tab Switching behavior
    struct TabSwitchComponent {
        int TargetTabId;
        bool Active;

        TabSwitchComponent(int targetId = 0) : TargetTabId(targetId), Active(false) {}
        TabSwitchComponent& SetTargetTabId(int id) { TargetTabId = id; return *this; }
    };
}
