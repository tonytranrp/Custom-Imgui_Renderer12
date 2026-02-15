#pragma once
#include <functional>
#include "imgui.h"

namespace RenderUtils {
    
    enum class SliderDataType {
        Float,
        Int
    };

    struct SliderComponent {
        // Data
        float Value; // Stored as float for simplicity, cast to int if needed
        float Min;
        float Max;
        SliderDataType DataType = SliderDataType::Float;

        // Visual / Animation
        float VisualValue; // The value currently being displayed (for lerping)
        bool EnableSmoothing = false;
        float SmoothingSpeed = 10.0f; // Higher = faster
        bool IsDragging = false;

        // Styling
        float TrackHeight = 4.0f;
        float KnobRadius = 8.0f;
        
        ImU32 ColorTrack = IM_COL32(80, 80, 90, 255);
        ImU32 ColorFill = IM_COL32(100, 180, 255, 255);
        ImU32 ColorKnob = IM_COL32(255, 255, 255, 255);
        
        // Interaction
        float ScrollSensitivity = 1.0f; // Multiplier for how fast it moves per pixel dragged (1.0 = standard)

        std::function<void(float)> OnChange;

        SliderComponent(float value = 0.0f, float min = 0.0f, float max = 1.0f)
            : Value(value), Min(min), Max(max), VisualValue(value), IsDragging(false) {}

        // --- Fluent Builder API ---

        SliderComponent& SetValue(float val) { 
            Value = val; 
            if(!EnableSmoothing) VisualValue = val;
            return *this; 
        }
        
        SliderComponent& SetRange(float min, float max) { Min = min; Max = max; return *this; }
        
        SliderComponent& SetDataType(SliderDataType type) { DataType = type; return *this; }
        
        // Style Setters
        SliderComponent& SetColors(ImU32 track, ImU32 fill, ImU32 knob) {
            ColorTrack = track;
            ColorFill = fill;
            ColorKnob = knob;
            return *this;
        }
        
        SliderComponent& SetSizes(float trackHeight, float knobRadius) {
            TrackHeight = trackHeight;
            KnobRadius = knobRadius;
            return *this;
        }

        // Animation / Behavior
        SliderComponent& SetSmoothing(bool enable, float speed = 10.0f) {
            EnableSmoothing = enable;
            SmoothingSpeed = speed;
            return *this;
        }
        
        SliderComponent& SetSensitivity(float sensitivity) {
            ScrollSensitivity = sensitivity;
            return *this;
        }

        SliderComponent& SetOnChange(std::function<void(float)> callback) { OnChange = callback; return *this; }
        
        // Helpers
        SliderComponent& AsInt() { DataType = SliderDataType::Int; return *this; }
    };
}
