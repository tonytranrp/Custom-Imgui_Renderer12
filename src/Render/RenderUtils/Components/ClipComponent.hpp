#pragma once

namespace RenderUtils {
    // Clipping Behavior
    struct ClipComponent {
        bool ClipChildren; 

        ClipComponent(bool clip = true) : ClipChildren(clip) {}

        ClipComponent& SetClipChildren(bool clip) { ClipChildren = clip; return *this; }
    };
}
