#pragma once

#include "UIRenderer.hpp"
#include "UIComponents.hpp" // Now includes all component headers
#include "entt/entt.hpp"

namespace RenderUtils {

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
            m_Registry.emplace<ContainerComponent>(entity, Type, name);
            m_Registry.emplace<InputStateComponent>(entity);
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
            if (m_Entity != entt::null && childBuilder.m_Entity != entt::null) {
                auto& pc = m_Registry.emplace_or_replace<ParentComponent>(childBuilder.m_Entity);
                pc.ParentEntity = m_Entity;
                
                // Inherit ZIndex logic
                if (m_Registry.all_of<StyleComponent>(m_Entity)) {
                    int parentZ = m_Registry.get<StyleComponent>(m_Entity).ZIndexInt;
                    if (m_Registry.all_of<StyleComponent>(childBuilder.m_Entity)) {
                         auto& childStyle = m_Registry.get<StyleComponent>(childBuilder.m_Entity);
                         // Logic: If parent is Normal, child is Above. If parent is Above, child is +1 (int logic)
                         // Since we switched to Enum, we should probably stick to int for layering or use the enum logic properly.
                         // For now, let's keep the simple increment logic using the cached int.
                         childStyle.ZIndexInt = parentZ + 1;
                         // We don't update 'Layer' enum back because it might not map to a named enum value, which is fine.
                    }
                }

                // Snap Position if Child has Transform and Parent has Transform
                if (m_Registry.all_of<TransformComponent>(m_Entity) && m_Registry.all_of<TransformComponent>(childBuilder.m_Entity)) {
                    const auto& parentTrans = m_Registry.get<TransformComponent>(m_Entity);
                    auto& childTrans = m_Registry.get<TransformComponent>(childBuilder.m_Entity);
                    // If child position is meant to be relative, we add parent pos.
                    // Assuming the 'With<Transform>' set the relative position.
                    // Save relative offset
                    pc.RelativeOffset = childTrans.Position;
                    childTrans.Position.x += parentTrans.Position.x;
                    childTrans.Position.y += parentTrans.Position.y;
                }
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
