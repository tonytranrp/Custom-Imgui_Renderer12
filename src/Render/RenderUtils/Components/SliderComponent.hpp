#pragma once
#include <functional>

namespace RenderUtils {
    struct SliderComponent {
        float Value;
        float Min;
        float Max;
        bool IsDragging;
        std::function<void(float)> OnChange;

        SliderComponent(float value = 0.0f, float min = 0.0f, float max = 1.0f)
            : Value(value), Min(min), Max(max), IsDragging(false) {}

        SliderComponent& SetValue(float val) { Value = val; return *this; }
        SliderComponent& SetMin(float min) { Min = min; return *this; }
        SliderComponent& SetMax(float max) { Max = max; return *this; }
        SliderComponent& SetOnChange(std::function<void(float)> callback) { OnChange = callback; return *this; }
    };
}
