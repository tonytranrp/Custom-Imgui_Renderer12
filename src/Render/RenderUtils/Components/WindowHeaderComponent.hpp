#pragma once

#include <algorithm>

#include "entt/entt.hpp"
#include "imgui.h"

#include "ContainerComponent.hpp"
#include "TransformComponent.hpp"

namespace RenderUtils {
    struct WindowHeaderComponent {
        bool Enabled = true;
        float Height = 30.0f;
        ImU32 BackgroundColor = IM_COL32(40, 40, 50, 255);
        ImU32 TextColor = IM_COL32(255, 255, 255, 255);
        ImU32 SeparatorColor = IM_COL32(0, 0, 0, 100);
        float PaddingX = 10.0f;
        float PaddingY = 7.0f;
        bool ShowTitle = true;
        bool DragFromHeaderOnly = true;
        bool ClipChildrenBelowHeader = true;

        WindowHeaderComponent& SetEnabled(bool enabled) {
            Enabled = enabled;
            return *this;
        }

        WindowHeaderComponent& SetHeight(float height) {
            Height = height;
            return *this;
        }

        WindowHeaderComponent& SetBackgroundColor(ImU32 color) {
            BackgroundColor = color;
            return *this;
        }

        WindowHeaderComponent& SetTextColor(ImU32 color) {
            TextColor = color;
            return *this;
        }

        WindowHeaderComponent& SetSeparatorColor(ImU32 color) {
            SeparatorColor = color;
            return *this;
        }

        WindowHeaderComponent& SetPadding(float x, float y) {
            PaddingX = x;
            PaddingY = y;
            return *this;
        }

        WindowHeaderComponent& SetShowTitle(bool showTitle) {
            ShowTitle = showTitle;
            return *this;
        }

        WindowHeaderComponent& SetDragFromHeaderOnly(bool dragFromHeaderOnly) {
            DragFromHeaderOnly = dragFromHeaderOnly;
            return *this;
        }

        WindowHeaderComponent& SetClipChildrenBelowHeader(bool clipChildrenBelowHeader) {
            ClipChildrenBelowHeader = clipChildrenBelowHeader;
            return *this;
        }
    };

    namespace WindowHeaderSystem {
        inline const WindowHeaderComponent* GetOrNull(const entt::registry& registry, entt::entity entity) {
            if (!registry.valid(entity)) {
                return nullptr;
            }

            const auto* container = registry.try_get<ContainerComponent>(entity);
            if (!container || container->Type != ContainerType::Window) {
                return nullptr;
            }

            return registry.try_get<WindowHeaderComponent>(entity);
        }

        inline float GetEffectiveHeaderHeight(const entt::registry& registry, entt::entity entity) {
            if (!registry.valid(entity)) {
                return 0.0f;
            }

            const auto* container = registry.try_get<ContainerComponent>(entity);
            if (!container || container->Type != ContainerType::Window) {
                return 0.0f;
            }

            const auto* header = registry.try_get<WindowHeaderComponent>(entity);
            if (!header) {
                return 30.0f;
            }

            if (!header->Enabled) {
                return 0.0f;
            }
            return (std::max)(0.0f, header->Height);
        }

        inline float GetContentTopInset(const entt::registry& registry, entt::entity entity) {
            if (!registry.valid(entity)) {
                return 0.0f;
            }

            const auto* container = registry.try_get<ContainerComponent>(entity);
            if (!container || container->Type != ContainerType::Window) {
                return 0.0f;
            }

            const auto* header = registry.try_get<WindowHeaderComponent>(entity);
            if (!header) {
                return 30.0f;
            }

            if (!header->Enabled || !header->ClipChildrenBelowHeader) {
                return 0.0f;
            }

            return (std::max)(0.0f, header->Height);
        }

        inline bool IsPointInHeader(const entt::registry& registry, entt::entity entity, const ImVec2& point) {
            const auto* transform = registry.try_get<TransformComponent>(entity);
            if (!transform) {
                return false;
            }

            const float headerHeight = GetEffectiveHeaderHeight(registry, entity);
            if (headerHeight <= 0.0f) {
                return false;
            }

            const ImVec2 min = transform->Position;
            const ImVec2 max = ImVec2(min.x + transform->Size.x, min.y + headerHeight);
            return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
        }

        inline void DrawHeader(entt::registry& registry, entt::entity entity, ImDrawList* drawList,
            const ImVec2& pMin, const ImVec2& pMax, float rounding, ImDrawFlags roundingFlags, float alphaMultiplier = 1.0f) {
            if (!drawList) {
                return;
            }

            const auto* container = registry.try_get<ContainerComponent>(entity);
            if (!container || container->Type != ContainerType::Window) {
                return;
            }

            WindowHeaderComponent defaults;
            const WindowHeaderComponent* header = registry.try_get<WindowHeaderComponent>(entity);
            if (!header) {
                header = &defaults;
            }

            if (!header->Enabled || header->Height <= 0.0f) {
                return;
            }

            if (alphaMultiplier < 0.0f) {
                alphaMultiplier = 0.0f;
            }
            if (alphaMultiplier > 1.0f) {
                alphaMultiplier = 1.0f;
            }

            auto applyAlpha = [alphaMultiplier](ImU32 color) {
                const int r = (color >> 0) & 0xFF;
                const int g = (color >> 8) & 0xFF;
                const int b = (color >> 16) & 0xFF;
                const int a = (color >> 24) & 0xFF;
                const int scaledA = static_cast<int>(static_cast<float>(a) * alphaMultiplier);
                return IM_COL32(r, g, b, scaledA);
            };

            const ImVec2 headerMax = ImVec2(pMax.x, pMin.y + header->Height);
            const ImDrawFlags headerRounding = roundingFlags & ImDrawFlags_RoundCornersTop;
            drawList->AddRectFilled(pMin, headerMax, applyAlpha(header->BackgroundColor), rounding, headerRounding);

            if (header->ShowTitle && container->Name && container->Name[0] != '\0') {
                drawList->AddText(
                    ImVec2(pMin.x + header->PaddingX, pMin.y + header->PaddingY),
                    applyAlpha(header->TextColor),
                    container->Name);
            }

            const ImU32 separatorColor = applyAlpha(header->SeparatorColor);
            if ((separatorColor & IM_COL32_A_MASK) != 0) {
                drawList->AddLine(ImVec2(pMin.x, headerMax.y), ImVec2(pMax.x, headerMax.y), separatorColor);
            }
        }
    } // namespace WindowHeaderSystem
} // namespace RenderUtils
