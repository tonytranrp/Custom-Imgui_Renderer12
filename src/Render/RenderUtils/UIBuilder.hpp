#pragma once

#include "UIRenderer.hpp"
#include "UIComponents.hpp" // Now includes all component headers
#include "entt/entt.hpp"
#include <cassert>
#include <type_traits>
#include <utility>

namespace RenderUtils {

    class UIBuilder; // Forward declaration

    class AnimationBuilder {
    private:
        entt::registry& m_Registry;
        entt::entity m_Entity;
        Animation m_Anim;

    public:
        AnimationBuilder(entt::registry& registry, entt::entity entity) 
            : m_Registry(registry), m_Entity(entity) {
            m_Anim.Duration = 1.0f;
            m_Anim.Easing = EasingType::Linear;
        }

        AnimationBuilder& Duration(float duration) {
            m_Anim.Duration = duration;
            return *this;
        }

        AnimationBuilder& Ease(EasingType easing) {
            m_Anim.Easing = easing;
            return *this;
        }

        AnimationBuilder& Loop(bool loop = true) {
            m_Anim.Loop = loop;
            return *this;
        }

        // Float Animation
        AnimationBuilder& Float(float start, float end, std::function<void(float)> apply) {
            m_Anim.StartVal.data = start;
            m_Anim.EndVal.data = end;
            m_Anim.Apply = [apply](const AnimationValue& val) {
                apply(std::get<float>(val.data));
            };
            return *this;
        }
        
        // Vec2 Animation
        AnimationBuilder& Vec2(ImVec2 start, ImVec2 end, std::function<void(ImVec2)> apply) {
            m_Anim.StartVal.data = start;
            m_Anim.EndVal.data = end;
            m_Anim.Apply = [apply](const AnimationValue& val) {
                apply(std::get<ImVec2>(val.data));
            };
            return *this;
        }

        // Color Animation
        AnimationBuilder& Color(ImU32 start, ImU32 end, std::function<void(ImU32)> apply) {
            m_Anim.StartVal.data = start;
            m_Anim.EndVal.data = end;
            m_Anim.Apply = [apply](const AnimationValue& val) {
                apply(std::get<ImU32>(val.data));
            };
            return *this;
        }
        
        // Custom Update
        AnimationBuilder& Custom(std::function<void(float, entt::registry&, entt::entity)> customUpdate) {
            m_Anim.CustomUpdate = customUpdate;
            return *this;
        }

        // Finish and Add
        void Start() {
             if (m_Entity != entt::null) {
                if (!m_Registry.any_of<AnimationComponent>(m_Entity)) {
                    m_Registry.emplace<AnimationComponent>(m_Entity);
                }
                m_Registry.get<AnimationComponent>(m_Entity).AddAnimation(m_Anim);
            }
        }
    };

    class UIBuilder {
    public:
        // Begin a new UI scene construction (wraps registry)
        static UIBuilder Begin(entt::registry& registry) {
            return UIBuilder(registry, entt::null);
        }

        // Create a new entity (Root level or Sibling)
        // Usage: .Create<ContainerType::Window>("Name")
        template <ContainerType Type>
        UIBuilder Create(const char* name) {
            entt::entity entity = m_Registry.create();
            m_Registry.emplace<TransformComponent>(entity); // Default
            m_Registry.emplace<StyleComponent>(entity);     // Default
            const char* safeName = name ? name : "";
            m_Registry.emplace<ContainerComponent>(entity, Type, safeName);
            m_Registry.emplace<InputStateComponent>(entity);
            if constexpr (Type == ContainerType::Window) {
                if (!m_Registry.any_of<WindowHeaderComponent>(entity)) {
                    m_Registry.emplace<WindowHeaderComponent>(entity);
                }
            }
            // Draggable defaults to false, Collision defaults to false (not added)
            
            return UIBuilder(m_Registry, entity);
        }

        // Draw this entity above another entity (by name)
        UIBuilder& DrawAbove(const char* targetName) {
            if (m_Entity != entt::null) {
                m_Registry.emplace_or_replace<DrawAboveComponent>(m_Entity, targetName);
            }
            return *this;
        }

        // Add Tab Switching Component
        UIBuilder& IsTab(int tabId) {
             if (m_Entity != entt::null) {
                m_Registry.emplace_or_replace<TabSwitchComponent>(m_Entity, tabId);
            }
            return *this;
        }

        // Add Tab Trigger Component
        UIBuilder& IsTabTrigger(int tabId, ImU32 activeColor = IM_COL32(80, 80, 100, 255), ImU32 inactiveColor = IM_COL32(60, 60, 70, 255)) {
            if (m_Entity != entt::null) {
                m_Registry.emplace_or_replace<TabTriggerComponent>(m_Entity, tabId, activeColor, inactiveColor);
            }
            return *this;
        }

        // Add a component to the current entity
        // Usage: .With<TransformComponent>(ImVec2(0,0), ImVec2(100,100))
        template <typename Component, typename... Args>
        UIBuilder& With(Args&&... args) {
            if (m_Entity != entt::null) {
                m_Registry.emplace_or_replace<Component>(m_Entity, std::forward<Args>(args)...);
            }
            return *this;
        }

        // Add a child entity using a nested builder
        // Usage: .Child( UIBuilder::Begin(reg).Create<...>(...).With(...) )
        // OR easier: .Child( UIBuilder::Create<...>(...) ) -- wait, Create needs registry.
        // Better: .Child( [&](UIBuilder& b) { b.Create... } ) ?
        // Or simply: .Child( UIBuilder::Begin(m_Registry).Create... )
        
        UIBuilder& Child(const UIBuilder& childBuilder) {
            // ... (existing implementation)
            if (&m_Registry != &childBuilder.m_Registry) {
                assert(false && "UIBuilder::Child called with builders from different registries");
                return *this;
            }

            if (m_Entity != entt::null && childBuilder.m_Entity != entt::null &&
                m_Registry.valid(m_Entity) && m_Registry.valid(childBuilder.m_Entity)) {
                auto& pc = m_Registry.emplace_or_replace<ParentComponent>(childBuilder.m_Entity);
                pc.ParentEntity = m_Entity;
                
                // Inherit ZIndex logic
                if (m_Registry.all_of<StyleComponent>(m_Entity)) {
                    int parentZ = m_Registry.get<StyleComponent>(m_Entity).ZIndexInt;
                    if (m_Registry.all_of<StyleComponent>(childBuilder.m_Entity)) {
                         auto& childStyle = m_Registry.get<StyleComponent>(childBuilder.m_Entity);
                         // Always increment Z-Index for children to ensure they draw on top of parent
                         childStyle.ZIndexInt = parentZ + 1;
                    }
                }

                // Snap Position if Child has Transform and Parent has Transform
                if (m_Registry.all_of<TransformComponent>(m_Entity) && m_Registry.all_of<TransformComponent>(childBuilder.m_Entity)) {
                    auto& childTrans = m_Registry.get<TransformComponent>(childBuilder.m_Entity);
                    pc.RelativeOffset = childTrans.Position;
                }
            }
            return *this;
        }

        // Ergonomic child builder overload:
        // .Child<ContainerType::Panel>("MyChild", [](UIBuilder& child){ ... })
        template <ContainerType Type, typename Fn>
        UIBuilder& Child(const char* name, Fn&& configure) {
            static_assert(std::is_invocable_v<Fn, UIBuilder&>, "UIBuilder::Child configure callback must accept UIBuilder&");
            if (m_Entity == entt::null || !m_Registry.valid(m_Entity)) {
                return *this;
            }

            UIBuilder child = UIBuilder::Begin(m_Registry).Create<Type>(name);
            std::forward<Fn>(configure)(child);
            child.End();
            return Child(child);
        }

        // Add Animation (Fluent API)
        AnimationBuilder Animate() {
            return AnimationBuilder(m_Registry, m_Entity);
        }

        // Legacy Animate (Keep for compatibility if needed, or remove)
        UIBuilder& AnimateLegacy(float duration, EasingType easing, std::function<void(Animation&)> setup) {
            if (m_Entity != entt::null) {
                if (!m_Registry.any_of<AnimationComponent>(m_Entity)) {
                    m_Registry.emplace<AnimationComponent>(m_Entity);
                }
                auto& animComp = m_Registry.get<AnimationComponent>(m_Entity);
                Animation anim;
                anim.Duration = duration;
                anim.Easing = easing;
                setup(anim);
                animComp.AddAnimation(anim);
            }
            return *this;
        }

        // Finish current entity configuration (optional, just returns reference)
        UIBuilder& End() {
            return *this;
        }

        // Accessor
        entt::entity Entity() const { return m_Entity; }
        entt::registry& Registry() const { return m_Registry; }

    private:
        entt::registry& m_Registry;
        entt::entity m_Entity;

        UIBuilder(entt::registry& registry, entt::entity entity) 
            : m_Registry(registry), m_Entity(entity) {}
    };

}
