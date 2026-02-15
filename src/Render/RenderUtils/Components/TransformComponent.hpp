#pragma once
#include "imgui.h"

namespace RenderUtils {
    // Basic Transform for UI elements with Fluent API
    struct TransformComponent {
        ImVec2 Position;
        ImVec2 Size;
        ImVec2 Scale;
        ImVec2 Pivot; // (0,0) = Top-Left, (0.5,0.5) = Center, (1,1) = Bottom-Right
        float Rotation; // In degrees

        TransformComponent(const ImVec2& pos = ImVec2(0,0), const ImVec2& size = ImVec2(0,0)) 
            : Position(pos), Size(size), Scale(ImVec2(1.0f, 1.0f)), Pivot(ImVec2(0.0f, 0.0f)), Rotation(0.0f) {}

        // --- Fluent Builder API ---

        TransformComponent& SetPosition(const ImVec2& pos) {
            Position = pos;
            return *this;
        }

        TransformComponent& SetSize(const ImVec2& size) {
            Size = size;
            return *this;
        }

        TransformComponent& SetScale(const ImVec2& scale) {
            Scale = scale;
            return *this;
        }

        TransformComponent& SetScale(float scale) {
            Scale = ImVec2(scale, scale);
            return *this;
        }

        TransformComponent& SetRotation(float degrees) {
            Rotation = degrees;
            return *this;
        }

        TransformComponent& SetPivot(const ImVec2& pivot) {
            Pivot = pivot;
            return *this;
        }
    };
}
