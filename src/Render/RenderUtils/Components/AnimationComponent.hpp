#pragma once
#include "imgui.h"
#include <vector>
#include <functional>
#include <cmath>
#include <variant>

namespace RenderUtils {

    enum class EasingType {
        Linear,
        EaseInQuad,
        EaseOutQuad,
        EaseInOutQuad,
        EaseInCubic,
        EaseOutCubic,
        EaseInOutCubic,
        ElasticOut,
        BounceOut
    };

    struct AnimationValue {
        std::variant<float, ImVec2, ImU32> data;
    };

    struct Animation {
        // Unique ID for the animation (optional, for cancellation)
        int ID;
        
        // Duration and Timing
        float Duration;
        float Elapsed;
        float StartTime; // Relative to system start, or 0 if using elapsed
        bool Loop;
        bool Finished;

        // Easing
        EasingType Easing;

        // Values
        AnimationValue StartVal;
        AnimationValue EndVal;

        // Callback to apply value
        std::function<void(const AnimationValue&)> Apply;
        
        // Generic Custom Update Callback
        // Arguments: easedT (0.0 to 1.0), Registry, Entity
        std::function<void(float easedT, entt::registry&, entt::entity)> CustomUpdate;

        // Completion Callback
        std::function<void()> OnComplete;

        Animation() : ID(0), Duration(1.0f), Elapsed(0.0f), StartTime(0.0f), Loop(false), Finished(false), Easing(EasingType::Linear) {}
    };

    struct AnimationComponent {
        std::vector<Animation> Animations;

        AnimationComponent() {}

        void AddAnimation(const Animation& anim) {
            Animations.push_back(anim);
        }
    };

    // Easing Functions Helper
    class Easing {
    public:
        static float Apply(float t, EasingType type) {
            if (t < 0) t = 0;
            if (t > 1) t = 1;

            switch (type) {
            case EasingType::Linear: return t;
            case EasingType::EaseInQuad: return t * t;
            case EasingType::EaseOutQuad: return t * (2 - t);
            case EasingType::EaseInOutQuad: return t < .5 ? 2 * t * t : -1 + (4 - 2 * t) * t;
            case EasingType::EaseInCubic: return t * t * t;
            case EasingType::EaseOutCubic: return (--t) * t * t + 1;
            case EasingType::EaseInOutCubic: return t < .5 ? 4 * t * t * t : (t - 1) * (2 * t - 2) * (2 * t - 2) + 1;
            case EasingType::ElasticOut: {
                float c4 = (2 * 3.14159f) / 3;
                return t == 0 ? 0 : t == 1 ? 1 : (float)(pow(2, -10 * t) * sin((t * 10 - 0.75f) * c4) + 1);
            }
            case EasingType::BounceOut: {
                float n1 = 7.5625f;
                float d1 = 2.75f;
                if (t < 1 / d1) {
                    return n1 * t * t;
                } else if (t < 2 / d1) {
                    return n1 * (t -= 1.5f / d1) * t + 0.75f;
                } else if (t < 2.5 / d1) {
                    return n1 * (t -= 2.25f / d1) * t + 0.9375f;
                } else {
                    return n1 * (t -= 2.625f / d1) * t + 0.984375f;
                }
            }
            default: return t;
            }
        }
    };
}
