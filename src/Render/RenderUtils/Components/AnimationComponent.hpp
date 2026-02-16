#pragma once
#include "imgui.h"
#include "entt/entt.hpp"
#include <algorithm>
#include <atomic>
#include <vector>
#include <functional>
#include <cmath>
#include <string>
#include <utility>
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
        std::variant<float, int, ImVec2, ImVec4, ImU32> data;
    };

    struct Animation {
        // Unique ID for the animation (optional, for cancellation)
        int ID;
        std::string Tag;
        
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
            AddAnimationEx(anim);
        }

        int AddAnimationEx(Animation anim) {
            static std::atomic<int> s_NextAnimationId{ 1 };
            if (anim.ID <= 0) {
                anim.ID = s_NextAnimationId.fetch_add(1, std::memory_order_relaxed);
            }
            Animations.push_back(std::move(anim));
            return Animations.back().ID;
        }

        bool CancelById(int id) {
            auto it = std::remove_if(Animations.begin(), Animations.end(), [id](const Animation& anim) {
                return anim.ID == id;
            });
            const bool removed = it != Animations.end();
            Animations.erase(it, Animations.end());
            return removed;
        }

        size_t CancelByTag(const std::string& tag) {
            auto it = std::remove_if(Animations.begin(), Animations.end(), [&tag](const Animation& anim) {
                return anim.Tag == tag;
            });
            const size_t removed = static_cast<size_t>(Animations.end() - it);
            Animations.erase(it, Animations.end());
            return removed;
        }

        void ClearAll() {
            Animations.clear();
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

    namespace AnimationSystem {
        namespace detail {
            inline ImU32 LerpColor(ImU32 start, ImU32 end, float t) {
                const int sR = (start >> 0) & 0xFF;
                const int sG = (start >> 8) & 0xFF;
                const int sB = (start >> 16) & 0xFF;
                const int sA = (start >> 24) & 0xFF;

                const int eR = (end >> 0) & 0xFF;
                const int eG = (end >> 8) & 0xFF;
                const int eB = (end >> 16) & 0xFF;
                const int eA = (end >> 24) & 0xFF;

                const int cR = sR + static_cast<int>((eR - sR) * t);
                const int cG = sG + static_cast<int>((eG - sG) * t);
                const int cB = sB + static_cast<int>((eB - sB) * t);
                const int cA = sA + static_cast<int>((eA - sA) * t);
                return IM_COL32(cR, cG, cB, cA);
            }

            inline bool InterpolateValue(const AnimationValue& start, const AnimationValue& end, float t, AnimationValue& outValue) {
                if (const auto* startFloat = std::get_if<float>(&start.data)) {
                    if (const auto* endFloat = std::get_if<float>(&end.data)) {
                        outValue.data = *startFloat + (*endFloat - *startFloat) * t;
                        return true;
                    }
                }

                if (const auto* startInt = std::get_if<int>(&start.data)) {
                    if (const auto* endInt = std::get_if<int>(&end.data)) {
                        const float current = static_cast<float>(*startInt) + (static_cast<float>(*endInt - *startInt) * t);
                        outValue.data = static_cast<int>(std::round(current));
                        return true;
                    }
                }

                if (const auto* startVec2 = std::get_if<ImVec2>(&start.data)) {
                    if (const auto* endVec2 = std::get_if<ImVec2>(&end.data)) {
                        outValue.data = ImVec2(
                            startVec2->x + (endVec2->x - startVec2->x) * t,
                            startVec2->y + (endVec2->y - startVec2->y) * t
                        );
                        return true;
                    }
                }

                if (const auto* startVec4 = std::get_if<ImVec4>(&start.data)) {
                    if (const auto* endVec4 = std::get_if<ImVec4>(&end.data)) {
                        outValue.data = ImVec4(
                            startVec4->x + (endVec4->x - startVec4->x) * t,
                            startVec4->y + (endVec4->y - startVec4->y) * t,
                            startVec4->z + (endVec4->z - startVec4->z) * t,
                            startVec4->w + (endVec4->w - startVec4->w) * t
                        );
                        return true;
                    }
                }

                if (const auto* startColor = std::get_if<ImU32>(&start.data)) {
                    if (const auto* endColor = std::get_if<ImU32>(&end.data)) {
                        outValue.data = LerpColor(*startColor, *endColor, t);
                        return true;
                    }
                }

                return false;
            }
        } // namespace detail

        inline void Update(entt::registry& registry, float deltaTime) {
            auto view = registry.view<AnimationComponent>();
            view.each([deltaTime, &registry](const auto entity, auto& animComp) {
                for (auto& anim : animComp.Animations) {
                    if (anim.Finished) {
                        continue;
                    }

                    const float duration = anim.Duration;
                    float t = 1.0f;
                    if (duration > 0.0f) {
                        anim.Elapsed += deltaTime;
                        t = anim.Elapsed / duration;
                    } else {
                        anim.Elapsed = duration;
                    }

                    if (t >= 1.0f) {
                        if (anim.Loop) {
                            if (duration > 0.0f) {
                                anim.Elapsed = std::fmod(anim.Elapsed, duration);
                                if (anim.Elapsed < 0.0f) {
                                    anim.Elapsed = 0.0f;
                                }
                                t = anim.Elapsed / duration;
                            } else {
                                t = 0.0f;
                            }
                        } else {
                            t = 1.0f;
                            anim.Finished = true;
                        }
                    } else if (t < 0.0f) {
                        t = 0.0f;
                    }

                    const float easedT = Easing::Apply(t, anim.Easing);
                    AnimationValue currentVal = anim.StartVal;
                    if (!detail::InterpolateValue(anim.StartVal, anim.EndVal, easedT, currentVal)) {
                        currentVal = (anim.Finished ? anim.EndVal : anim.StartVal);
                    }

                    if (anim.Apply) {
                        anim.Apply(currentVal);
                    }

                    if (anim.CustomUpdate) {
                        anim.CustomUpdate(easedT, registry, entity);
                    }

                    if (anim.Finished && anim.OnComplete) {
                        anim.OnComplete();
                    }
                }

                animComp.Animations.erase(
                    std::remove_if(animComp.Animations.begin(), animComp.Animations.end(),
                        [](const Animation& animation) { return animation.Finished; }),
                    animComp.Animations.end());
            });
        }
    } // namespace AnimationSystem
}
