#pragma once
#include "imgui.h"

namespace RenderUtils {
    // Enum for Drag Mode
    enum class DragMode {
        None,
        Free,
        HorizontalOnly,
        VerticalOnly
    };

    // Enum for Drag Constraints
    enum class DragConstraint {
        None,
        Parent,
        Window,
        Screen
    };

    // Interaction Components
    struct DraggableComponent {
        bool IsDragging;
        ImVec2 DragOffset;
        DragMode Mode;
        DragConstraint Constraint;

        DraggableComponent(DragMode mode = DragMode::None, DragConstraint constraint = DragConstraint::None, const ImVec2& offset = ImVec2(0,0), bool isDragging = false)
            : IsDragging(isDragging), DragOffset(offset), Mode(mode), Constraint(constraint) {}

        DraggableComponent& SetMode(DragMode mode) { Mode = mode; return *this; }
        DraggableComponent& SetConstraint(DragConstraint constraint) { Constraint = constraint; return *this; }
        DraggableComponent& SetDragOffset(const ImVec2& offset) { DragOffset = offset; return *this; }
        DraggableComponent& SetDragging(bool dragging) { IsDragging = dragging; return *this; }
    };
}
