#pragma once

#include <algorithm>
#include <cfloat>
#include <vector>

#include "entt/entt.hpp"
#include "imgui.h"

#include "CollisionComponent.hpp"
#include "ContainerComponent.hpp"
#include "DraggableComponent.hpp"
#include "ExpandComponent.hpp"
#include "LockedComponent.hpp"
#include "ParentComponent.hpp"
#include "ScrollComponent.hpp"
#include "SliderComponent.hpp"
#include "ShapeComponent.hpp"
#include "StyleComponent.hpp"
#include "TextComponent.hpp"
#include "TextInputComponent.hpp"
#include "TransformComponent.hpp"
#include "WindowHeaderComponent.hpp"

namespace RenderUtils {
    struct InputStateComponent {
        bool IsHovered;
        bool IsClicked;
        bool WasHovered;
        bool WasClicked;
        bool JustPressed;
        bool JustReleased;
        bool BlockInput; // If true, this entity consumes input and prevents it from passing through
        ImVec4 ClipRect; // (MinX, MinY, MaxX, MaxY) - The visible region of this entity (calculated by ResolveTransforms)

        InputStateComponent(bool hovered = false, bool clicked = false, bool block = true)
            : IsHovered(hovered),
              IsClicked(clicked),
              WasHovered(false),
              WasClicked(false),
              JustPressed(false),
              JustReleased(false),
              BlockInput(block),
              ClipRect(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX) {}

        InputStateComponent& SetHovered(bool hovered) { IsHovered = hovered; return *this; }
        InputStateComponent& SetClicked(bool clicked) { IsClicked = clicked; return *this; }
        InputStateComponent& SetBlockInput(bool block) { BlockInput = block; return *this; }
    };

    namespace InputStateSystem {
        namespace detail {
            inline void UpdateEdges(InputStateComponent& inputState, bool previousClicked) {
                inputState.JustPressed = (!previousClicked && inputState.IsClicked);
                inputState.JustReleased = (previousClicked && !inputState.IsClicked);
            }

            inline std::vector<ImVec4> CalculatePrecisionTextRects(
                entt::registry& registry,
                entt::entity entity,
                const TransformComponent& transform) {
                const auto* textComp = registry.try_get<TextComponent>(entity);
                if (!textComp || !textComp->PrecisionMode()) {
                    return {};
                }

                float paddingX = 10.0f;
                float paddingY = 10.0f;
                if (const auto* style = registry.try_get<StyleComponent>(entity)) {
                    paddingX = style->ContentPaddingX;
                    paddingY = style->ContentPaddingY;
                }

                const float topInset = WindowHeaderSystem::GetContentTopInset(registry, entity);
                return TextLayout::CalculateTextLines(*textComp, transform, paddingX, paddingY, topInset);
            }
        }

        inline void Update(entt::registry& registry, bool debugMode, entt::entity selectedEntity) {
            const ImVec2 mousePos = ImGui::GetMousePos();
            const bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);

            // Higher Z should consume input first.
            registry.sort<StyleComponent>([](const auto& lhs, const auto& rhs) {
                return lhs.ZIndexInt > rhs.ZIndexInt;
            });

            auto inputView = registry.view<StyleComponent, TransformComponent, InputStateComponent>();
            inputView.use<StyleComponent>();

            bool inputCaptured = false;

            for (auto entity : inputView) {
                auto& transform = inputView.get<TransformComponent>(entity);
                auto& inputState = inputView.get<InputStateComponent>(entity);
                auto& style = inputView.get<StyleComponent>(entity);

                const bool previousHovered = inputState.IsHovered;
                const bool previousClicked = inputState.IsClicked;
                inputState.WasHovered = previousHovered;
                inputState.WasClicked = previousClicked;
                inputState.JustPressed = false;
                inputState.JustReleased = false;

                if (!style.CalculatedVisible) {
                    inputState.IsHovered = false;
                    inputState.IsClicked = false;
                    detail::UpdateEdges(inputState, previousClicked);
                    continue;
                }

                bool isDragging = false;
                if (registry.any_of<DraggableComponent>(entity)) {
                    isDragging = registry.get<DraggableComponent>(entity).IsDragging;
                }
                if (isDragging) {
                    inputCaptured = true;
                }

                ImVec2 p_min = transform.Position;
                ImVec2 p_max = ImVec2(p_min.x + transform.Size.x, p_min.y + transform.Size.y);
                if (registry.any_of<ExpandComponent>(entity)) {
                    const auto& expand = registry.get<ExpandComponent>(entity);
                    if (expand.IsExpanded) {
                        if (expand.Direction == ExpandDirection::Down) {
                            p_max.y += expand.CurrentHeight;
                        } else {
                            p_min.y -= expand.CurrentHeight;
                        }
                    }
                }

                bool hovered = false;
                bool usedShapeHitTest = false;
                if (registry.any_of<ShapeComponent>(entity)) {
                    const auto& shape = registry.get<ShapeComponent>(entity);
                    if (shape.Enabled && shape.UseForHitTest) {
                        hovered = ShapeSystem::HitTest(shape, mousePos, p_min, p_max);
                        usedShapeHitTest = true;
                    }
                }
                if (!usedShapeHitTest) {
                    hovered = (mousePos.x >= p_min.x && mousePos.x <= p_max.x &&
                               mousePos.y >= p_min.y && mousePos.y <= p_max.y);
                }

                if (mousePos.x < inputState.ClipRect.x || mousePos.x > inputState.ClipRect.z ||
                    mousePos.y < inputState.ClipRect.y || mousePos.y > inputState.ClipRect.w) {
                    hovered = false;
                }

                if (inputCaptured && !isDragging) {
                    inputState.IsHovered = false;
                    inputState.IsClicked = false;
                    detail::UpdateEdges(inputState, previousClicked);
                    continue;
                }

                if (hovered) {
                    if (inputState.BlockInput) {
                        inputCaptured = true;
                    }

                    inputState.IsHovered = true;
                    if (registry.any_of<ScrollComponent>(entity)) {
                        auto& scroll = registry.get<ScrollComponent>(entity);
                        scroll.ViewHeight = transform.Size.y;
                        if (scroll.ContentHeight > scroll.ViewHeight) {
                            const float wheel = ImGui::GetIO().MouseWheel;
                            if (wheel != 0.0f) {
                                scroll.ScrollY -= wheel * scroll.Speed;
                                if (scroll.ScrollY < 0.0f) {
                                    scroll.ScrollY = 0.0f;
                                }
                                const float maxScroll = scroll.ContentHeight - scroll.ViewHeight;
                                if (scroll.ScrollY > maxScroll) {
                                    scroll.ScrollY = maxScroll;
                                }
                            }
                        } else {
                            scroll.ScrollY = 0.0f;
                        }
                    }

                    if (mouseClicked) {
                        inputState.IsClicked = true;
                    } else if (!mouseDown) {
                        inputState.IsClicked = false;
                    }
                } else {
                    inputState.IsHovered = false;
                    // Avoid stale click-hold when leaving clipped/visible area.
                    if (!isDragging || !mouseDown) {
                        inputState.IsClicked = false;
                    }
                }

                if (registry.any_of<TextInputComponent>(entity)) {
                    auto& textInput = registry.get<TextInputComponent>(entity);
                    if (mouseClicked) {
                        textInput.IsFocused = hovered;
                    }
                }

                if (registry.any_of<SliderComponent>(entity)) {
                    auto& slider = registry.get<SliderComponent>(entity);
                    if (hovered && mouseClicked) {
                        slider.IsDragging = true;
                    }
                    if (!mouseDown) {
                        slider.IsDragging = false;
                    }
                }

                detail::UpdateEdges(inputState, previousClicked);
            }

            auto dragView = registry.view<TransformComponent, DraggableComponent, InputStateComponent>();
            for (auto entity : dragView) {
                auto& transform = dragView.get<TransformComponent>(entity);
                auto& draggable = dragView.get<DraggableComponent>(entity);
                const auto& inputState = dragView.get<InputStateComponent>(entity);

                if (registry.any_of<LockedComponent>(entity) && registry.get<LockedComponent>(entity).Locked) {
                    draggable.IsDragging = false;
                    continue;
                }

                const bool draggingAllowed = (draggable.Mode != DragMode::None) || debugMode;
                if (!draggingAllowed) {
                    continue;
                }

                const bool debugOverride = debugMode && (entity == selectedEntity);
                bool effectivelyHovered = inputState.IsHovered;
                if (debugOverride && !effectivelyHovered) {
                    if (mousePos.x >= transform.Position.x && mousePos.x <= transform.Position.x + transform.Size.x &&
                        mousePos.y >= transform.Position.y && mousePos.y <= transform.Position.y + transform.Size.y) {
                        effectivelyHovered = true;
                    }
                }

                if (effectivelyHovered && mouseClicked && !draggable.IsDragging) {
                    bool canDrag = true;
                    if (!debugMode && registry.all_of<ContainerComponent>(entity) &&
                        registry.get<ContainerComponent>(entity).Type == ContainerType::Window) {
                        const auto* header = registry.try_get<WindowHeaderComponent>(entity);
                        const bool dragFromHeaderOnly = !header || (header->Enabled && header->DragFromHeaderOnly);
                        if (dragFromHeaderOnly && !WindowHeaderSystem::IsPointInHeader(registry, entity, mousePos)) {
                            canDrag = false;
                        }
                    }

                    if (canDrag) {
                        draggable.IsDragging = true;
                        draggable.DragOffset = ImVec2(mousePos.x - transform.Position.x, mousePos.y - transform.Position.y);
                    }
                }

                if (!draggable.IsDragging) {
                    continue;
                }

                if (!mouseDown) {
                    draggable.IsDragging = false;
                    continue;
                }

                ImVec2 newPos = ImVec2(mousePos.x - draggable.DragOffset.x, mousePos.y - draggable.DragOffset.y);
                if (draggable.Mode == DragMode::HorizontalOnly) {
                    newPos.y = transform.Position.y;
                } else if (draggable.Mode == DragMode::VerticalOnly) {
                    newPos.x = transform.Position.x;
                }

                if (draggable.Constraint == DragConstraint::Parent && registry.any_of<ParentComponent>(entity)) {
                    auto& parentComp = registry.get<ParentComponent>(entity);
                    if (registry.valid(parentComp.ParentEntity) && registry.all_of<TransformComponent>(parentComp.ParentEntity)) {
                        const auto& parentTrans = registry.get<TransformComponent>(parentComp.ParentEntity);
                        const float topInset = WindowHeaderSystem::GetContentTopInset(registry, parentComp.ParentEntity);
                        float minX = parentTrans.Position.x;
                        float minY = parentTrans.Position.y + topInset;
                        float maxX = parentTrans.Position.x + parentTrans.Size.x - transform.Size.x;
                        float maxY = parentTrans.Position.y + parentTrans.Size.y - transform.Size.y;

                        if (registry.any_of<TextComponent>(entity)) {
                            const auto& textComp = registry.get<TextComponent>(entity);
                            if (textComp.PrecisionMode()) {
                                const auto lines = detail::CalculatePrecisionTextRects(registry, entity, transform);
                                if (!lines.empty()) {
                                    float vMinX = FLT_MAX;
                                    float vMaxX = -FLT_MAX;
                                    float vMinY = FLT_MAX;
                                    float vMaxY = -FLT_MAX;
                                    for (const auto& line : lines) {
                                        if (line.x < vMinX) vMinX = line.x;
                                        if (line.x + line.z > vMaxX) vMaxX = line.x + line.z;
                                        if (line.y < vMinY) vMinY = line.y;
                                        if (line.y + line.w > vMaxY) vMaxY = line.y + line.w;
                                    }

                                    const float offMinX = vMinX - transform.Position.x;
                                    const float offMaxX = vMaxX - transform.Position.x;
                                    const float offMinY = vMinY - transform.Position.y;
                                    const float offMaxY = vMaxY - transform.Position.y;

                                    minX = parentTrans.Position.x - offMinX;
                                    minY = parentTrans.Position.y - offMinY;
                                    maxX = (parentTrans.Position.x + parentTrans.Size.x) - offMaxX;
                                    maxY = (parentTrans.Position.y + parentTrans.Size.y) - offMaxY;
                                }
                            }
                        }

                        if (maxX < minX) maxX = minX;
                        if (maxY < minY) maxY = minY;
                        if (newPos.x < minX) newPos.x = minX;
                        if (newPos.x > maxX) newPos.x = maxX;
                        if (newPos.y < minY) newPos.y = minY;
                        if (newPos.y > maxY) newPos.y = maxY;
                    }
                }

                if (registry.any_of<CollisionComponent>(entity)) {
                    const auto& selfCollision = registry.get<CollisionComponent>(entity);
                    if (!selfCollision.Collides) {
                        transform.Position = newPos;
                        if (registry.any_of<ParentComponent>(entity)) {
                            auto& parentComp = registry.get<ParentComponent>(entity);
                            if (registry.valid(parentComp.ParentEntity) && registry.all_of<TransformComponent>(parentComp.ParentEntity)) {
                                const auto& parentTransform = registry.get<TransformComponent>(parentComp.ParentEntity);
                                parentComp.RelativeOffset = ImVec2(
                                    transform.Position.x - parentTransform.Position.x,
                                    transform.Position.y - parentTransform.Position.y
                                );
                            }
                        }
                        continue;
                    }

                    if (registry.any_of<StyleComponent>(entity) &&
                        !registry.get<StyleComponent>(entity).CalculatedVisible) {
                        transform.Position = newPos;
                        if (registry.any_of<ParentComponent>(entity)) {
                            auto& parentComp = registry.get<ParentComponent>(entity);
                            if (registry.valid(parentComp.ParentEntity) && registry.all_of<TransformComponent>(parentComp.ParentEntity)) {
                                const auto& parentTransform = registry.get<TransformComponent>(parentComp.ParentEntity);
                                parentComp.RelativeOffset = ImVec2(
                                    transform.Position.x - parentTransform.Position.x,
                                    transform.Position.y - parentTransform.Position.y
                                );
                            }
                        }
                        continue;
                    }

                    const ImVec2 oldPos = transform.Position;

                    std::vector<ImVec4> myRects;
                    bool iAmPrecision = false;
                    if (registry.any_of<TextComponent>(entity)) {
                        const auto& textComp = registry.get<TextComponent>(entity);
                        if (textComp.PrecisionMode()) {
                            iAmPrecision = true;
                            TransformComponent tempTransform = transform;
                            tempTransform.Position = newPos;
                            myRects = detail::CalculatePrecisionTextRects(registry, entity, tempTransform);
                        }
                    }
                    if (!iAmPrecision && registry.any_of<ShapeComponent>(entity)) {
                        const auto& shape = registry.get<ShapeComponent>(entity);
                        if (shape.Enabled && shape.UseForHitTest) {
                            const ImVec2 newMin = newPos;
                            const ImVec2 newMax = ImVec2(newPos.x + transform.Size.x, newPos.y + transform.Size.y);
                            const ImVec4 bounds = ShapeSystem::GetBounds(shape, newMin, newMax);
                            myRects.push_back(ImVec4(bounds.x, bounds.y, bounds.z - bounds.x, bounds.w - bounds.y));
                            iAmPrecision = true;
                        }
                    }
                    if (!iAmPrecision) {
                        myRects.push_back(ImVec4(newPos.x, newPos.y, transform.Size.x, transform.Size.y));
                    }

                    auto collisionView = registry.view<TransformComponent, CollisionComponent>();
                    for (auto other : collisionView) {
                        if (other == entity) {
                            continue;
                        }

                        const auto& otherCollision = collisionView.get<CollisionComponent>(other);
                        if (!otherCollision.Collides) {
                            continue;
                        }
                        if (registry.any_of<StyleComponent>(other) &&
                            !registry.get<StyleComponent>(other).CalculatedVisible) {
                            continue;
                        }

                        entt::entity parentA = entt::null;
                        entt::entity parentB = entt::null;
                        if (registry.any_of<ParentComponent>(entity)) parentA = registry.get<ParentComponent>(entity).ParentEntity;
                        if (registry.any_of<ParentComponent>(other)) parentB = registry.get<ParentComponent>(other).ParentEntity;
                        if (parentA != parentB) {
                            continue;
                        }

                        const auto& otherTransform = collisionView.get<TransformComponent>(other);
                        std::vector<ImVec4> otherRects;
                        bool otherPrecision = false;
                        if (registry.any_of<TextComponent>(other)) {
                            const auto& textComp = registry.get<TextComponent>(other);
                            if (textComp.PrecisionMode()) {
                                otherPrecision = true;
                                otherRects = detail::CalculatePrecisionTextRects(registry, other, otherTransform);
                            }
                        }
                        if (!otherPrecision && registry.any_of<ShapeComponent>(other)) {
                            const auto& shape = registry.get<ShapeComponent>(other);
                            if (shape.Enabled && shape.UseForHitTest) {
                                const ImVec2 otherMin = otherTransform.Position;
                                const ImVec2 otherMax = ImVec2(otherTransform.Position.x + otherTransform.Size.x, otherTransform.Position.y + otherTransform.Size.y);
                                const ImVec4 bounds = ShapeSystem::GetBounds(shape, otherMin, otherMax);
                                otherRects.push_back(ImVec4(bounds.x, bounds.y, bounds.z - bounds.x, bounds.w - bounds.y));
                                otherPrecision = true;
                            }
                        }
                        if (!otherPrecision) {
                            otherRects.push_back(ImVec4(otherTransform.Position.x, otherTransform.Position.y, otherTransform.Size.x, otherTransform.Size.y));
                        }

                        bool resolved = false;
                        for (const auto& myRect : myRects) {
                            const ImVec2 myMin = ImVec2(myRect.x, myRect.y);
                            const ImVec2 myMax = ImVec2(myRect.x + myRect.z, myRect.y + myRect.w);
                            const ImVec2 delta = ImVec2(newPos.x - oldPos.x, newPos.y - oldPos.y);
                            const ImVec2 oldRectMin = ImVec2(myMin.x - delta.x, myMin.y - delta.y);

                            for (const auto& otherRect : otherRects) {
                                const ImVec2 otherMin = ImVec2(otherRect.x, otherRect.y);
                                const ImVec2 otherMax = ImVec2(otherRect.x + otherRect.z, otherRect.y + otherRect.w);

                                if (myMin.x < otherMax.x && myMax.x > otherMin.x &&
                                    myMin.y < otherMax.y && myMax.y > otherMin.y) {
                                    float overlapLeft = (myMin.x + myRect.z) - otherMin.x;
                                    float overlapRight = (otherMin.x + otherRect.z) - myMin.x;
                                    float overlapTop = (myMin.y + myRect.w) - otherMin.y;
                                    float overlapBottom = (otherMin.y + otherRect.w) - myMin.y;

                                    bool wasLeft = (oldRectMin.x + myRect.z) <= otherMin.x + 1.0f;
                                    bool wasRight = oldRectMin.x >= otherMax.x - 1.0f;
                                    bool wasAbove = (oldRectMin.y + myRect.w) <= otherMin.y + 1.0f;
                                    bool wasBelow = oldRectMin.y >= otherMax.y - 1.0f;

                                    float minOverlap = FLT_MAX;
                                    int axis = 0;
                                    if (wasLeft && overlapLeft < minOverlap) { minOverlap = overlapLeft; axis = 1; }
                                    if (wasRight && overlapRight < minOverlap) { minOverlap = overlapRight; axis = 2; }
                                    if (wasAbove && overlapTop < minOverlap) { minOverlap = overlapTop; axis = 3; }
                                    if (wasBelow && overlapBottom < minOverlap) { minOverlap = overlapBottom; axis = 4; }
                                    if (axis == 0) {
                                        if (overlapLeft < minOverlap) { minOverlap = overlapLeft; axis = 1; }
                                        if (overlapRight < minOverlap) { minOverlap = overlapRight; axis = 2; }
                                        if (overlapTop < minOverlap) { minOverlap = overlapTop; axis = 3; }
                                        if (overlapBottom < minOverlap) { minOverlap = overlapBottom; axis = 4; }
                                    }

                                    if (axis == 1) newPos.x -= overlapLeft;
                                    else if (axis == 2) newPos.x += overlapRight;
                                    else if (axis == 3) newPos.y -= overlapTop;
                                    else if (axis == 4) newPos.y += overlapBottom;

                                    resolved = true;
                                    break;
                                }
                            }
                            if (resolved) {
                                break;
                            }
                        }
                    }
                }

                transform.Position = newPos;
                if (registry.any_of<ParentComponent>(entity)) {
                    auto& parentComp = registry.get<ParentComponent>(entity);
                    if (registry.valid(parentComp.ParentEntity) && registry.all_of<TransformComponent>(parentComp.ParentEntity)) {
                        const auto& parentTransform = registry.get<TransformComponent>(parentComp.ParentEntity);
                        parentComp.RelativeOffset = ImVec2(
                            transform.Position.x - parentTransform.Position.x,
                            transform.Position.y - parentTransform.Position.y
                        );
                    }
                }
            }

            auto childView = registry.view<TransformComponent, ParentComponent>();
            for (auto entity : childView) {
                auto& transform = childView.get<TransformComponent>(entity);
                const auto& parent = childView.get<ParentComponent>(entity);
                if (!registry.valid(parent.ParentEntity) || !registry.all_of<TransformComponent>(parent.ParentEntity)) {
                    continue;
                }

                const auto& parentTransform = registry.get<TransformComponent>(parent.ParentEntity);
                float scrollOffset = 0.0f;
                if (registry.all_of<ScrollComponent>(parent.ParentEntity)) {
                    scrollOffset = registry.get<ScrollComponent>(parent.ParentEntity).ScrollY;
                }

                transform.Position = ImVec2(
                    parentTransform.Position.x + parent.RelativeOffset.x,
                    parentTransform.Position.y + parent.RelativeOffset.y - scrollOffset
                );
            }
        }
    } // namespace InputStateSystem
}
