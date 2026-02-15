#pragma once
#include "imgui.h"

namespace RenderUtils {
    // Component for buttons that switch tabs
    struct TabTriggerComponent {
        int TabId;
        ImU32 ActiveColor;
        ImU32 InactiveColor;

        TabTriggerComponent(int id = 0, ImU32 active = IM_COL32(80, 80, 100, 255), ImU32 inactive = IM_COL32(60, 60, 70, 255))
            : TabId(id), ActiveColor(active), InactiveColor(inactive) {}
            
        TabTriggerComponent& SetTabId(int id) { TabId = id; return *this; }
        TabTriggerComponent& SetColors(ImU32 active, ImU32 inactive) { ActiveColor = active; InactiveColor = inactive; return *this; }
    };
}
