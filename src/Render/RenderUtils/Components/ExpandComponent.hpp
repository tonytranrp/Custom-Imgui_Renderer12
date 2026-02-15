#pragma once

namespace RenderUtils {
    enum class ExpandDirection { Down, Up };

    struct ExpandComponent {
        bool IsExpanded;
        float ExpandedHeight; // Desired Height
        float CurrentHeight;  // Actual rendered height (used for hit-testing and clipping)
        bool ClipToParent;    // If true, shorten height to fit in parent
        ExpandDirection Direction; // Calculated during render

        ExpandComponent(bool isExpanded = false, float height = 100.0f, bool clip = true) 
            : IsExpanded(isExpanded), ExpandedHeight(height), CurrentHeight(0.0f), ClipToParent(clip), Direction(ExpandDirection::Down) {}

        ExpandComponent& SetExpanded(bool expanded) { IsExpanded = expanded; return *this; }
        ExpandComponent& SetExpandedHeight(float height) { ExpandedHeight = height; return *this; }
        ExpandComponent& SetClipToParent(bool clip) { ClipToParent = clip; return *this; }
        ExpandComponent& SetDirection(ExpandDirection dir) { Direction = dir; return *this; }
    };
}
