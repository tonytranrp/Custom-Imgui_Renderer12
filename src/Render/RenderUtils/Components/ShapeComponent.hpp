#pragma once

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <vector>

#include "entt/entt.hpp"
#include "imgui.h"

namespace RenderUtils {
    enum class ShapeType {
        Rect,
        Circle,
        Line,
        Triangle,
        Polyline
    };

    struct ShapePrimitive {
        ShapeType Type = ShapeType::Rect;
        std::vector<ImVec2> Points;
        ImVec2 Offset = ImVec2(0.0f, 0.0f);
        ImVec2 Size = ImVec2(0.0f, 0.0f);
        float Radius = 0.0f;
        float Rounding = 0.0f;
        ImDrawFlags RoundingFlags = ImDrawFlags_RoundCornersAll;
        bool Filled = true;
        ImU32 FillColor = IM_COL32(255, 255, 255, 255);
        bool StrokeEnabled = false;
        ImU32 StrokeColor = IM_COL32(255, 255, 255, 255);
        float StrokeThickness = 1.0f;
        bool Visible = true;

        ShapePrimitive() = default;
        explicit ShapePrimitive(ShapeType type) : Type(type) {}
    };

    struct ShapeComponent {
        std::vector<ShapePrimitive> Shapes;
        bool Enabled = true;
        bool DrawBehindContent = true;
        bool ClipToEntity = true;
        bool UseForHitTest = true;
        int Priority = 0;
        std::function<void(entt::registry&, entt::entity, ImDrawList*, ImVec2, ImVec2)> OnDrawOverride;

        ShapeComponent& AddShape(const ShapePrimitive& primitive) {
            Shapes.push_back(primitive);
            return *this;
        }

        ShapeComponent& ClearShapes() {
            Shapes.clear();
            return *this;
        }

        ShapeComponent& SetEnabled(bool enabled) {
            Enabled = enabled;
            return *this;
        }

        ShapeComponent& SetDrawBehindContent(bool drawBehind) {
            DrawBehindContent = drawBehind;
            return *this;
        }

        ShapeComponent& SetClipToEntity(bool clipToEntity) {
            ClipToEntity = clipToEntity;
            return *this;
        }

        ShapeComponent& SetUseForHitTest(bool useForHitTest) {
            UseForHitTest = useForHitTest;
            return *this;
        }

        ShapeComponent& SetPriority(int priority) {
            Priority = priority;
            return *this;
        }

        ShapeComponent& SetOnDrawOverride(std::function<void(entt::registry&, entt::entity, ImDrawList*, ImVec2, ImVec2)> callback) {
            OnDrawOverride = std::move(callback);
            return *this;
        }
    };

    namespace ShapeSystem {
        namespace detail {
            inline ImVec2 Add(const ImVec2& a, const ImVec2& b) {
                return ImVec2(a.x + b.x, a.y + b.y);
            }

            inline float DistancePointToSegment(const ImVec2& point, const ImVec2& a, const ImVec2& b) {
                const ImVec2 ab = ImVec2(b.x - a.x, b.y - a.y);
                const float abLenSq = ab.x * ab.x + ab.y * ab.y;
                if (abLenSq <= 0.00001f) {
                    const float dx = point.x - a.x;
                    const float dy = point.y - a.y;
                    return std::sqrt(dx * dx + dy * dy);
                }

                const ImVec2 ap = ImVec2(point.x - a.x, point.y - a.y);
                float t = (ap.x * ab.x + ap.y * ab.y) / abLenSq;
                t = (std::max)(0.0f, (std::min)(1.0f, t));
                const ImVec2 closest = ImVec2(a.x + ab.x * t, a.y + ab.y * t);
                const float dx = point.x - closest.x;
                const float dy = point.y - closest.y;
                return std::sqrt(dx * dx + dy * dy);
            }

            inline bool PointInTriangle(const ImVec2& p, const ImVec2& a, const ImVec2& b, const ImVec2& c) {
                const ImVec2 v0 = ImVec2(c.x - a.x, c.y - a.y);
                const ImVec2 v1 = ImVec2(b.x - a.x, b.y - a.y);
                const ImVec2 v2 = ImVec2(p.x - a.x, p.y - a.y);

                const float dot00 = v0.x * v0.x + v0.y * v0.y;
                const float dot01 = v0.x * v1.x + v0.y * v1.y;
                const float dot02 = v0.x * v2.x + v0.y * v2.y;
                const float dot11 = v1.x * v1.x + v1.y * v1.y;
                const float dot12 = v1.x * v2.x + v1.y * v2.y;

                const float denom = dot00 * dot11 - dot01 * dot01;
                if (std::fabs(denom) <= 0.00001f) {
                    return false;
                }

                const float invDenom = 1.0f / denom;
                const float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
                const float v = (dot00 * dot12 - dot01 * dot02) * invDenom;
                return (u >= 0.0f && v >= 0.0f && (u + v) <= 1.0f);
            }

            inline ImVec2 ResolveSize(const ShapePrimitive& primitive, const ImVec2& entityMin, const ImVec2& entityMax) {
                const float entityW = entityMax.x - entityMin.x;
                const float entityH = entityMax.y - entityMin.y;
                return ImVec2(
                    primitive.Size.x > 0.0f ? primitive.Size.x : entityW,
                    primitive.Size.y > 0.0f ? primitive.Size.y : entityH
                );
            }

            inline void ExpandBounds(ImVec4& bounds, const ImVec2& point) {
                if (point.x < bounds.x) bounds.x = point.x;
                if (point.y < bounds.y) bounds.y = point.y;
                if (point.x > bounds.z) bounds.z = point.x;
                if (point.y > bounds.w) bounds.w = point.y;
            }

            inline void ExpandBoundsForPrimitive(ImVec4& bounds, const ShapePrimitive& primitive, const ImVec2& entityMin, const ImVec2& entityMax) {
                const ImVec2 base = Add(entityMin, primitive.Offset);
                const ImVec2 size = ResolveSize(primitive, entityMin, entityMax);
                const float strokePad = primitive.StrokeEnabled ? (primitive.StrokeThickness * 0.5f) : 0.0f;

                switch (primitive.Type) {
                case ShapeType::Rect: {
                    ExpandBounds(bounds, ImVec2(base.x - strokePad, base.y - strokePad));
                    ExpandBounds(bounds, ImVec2(base.x + size.x + strokePad, base.y + size.y + strokePad));
                    break;
                }
                case ShapeType::Circle: {
                    const float radius = primitive.Radius > 0.0f ? primitive.Radius : (std::min)(size.x, size.y) * 0.5f;
                    ExpandBounds(bounds, ImVec2(base.x - radius - strokePad, base.y - radius - strokePad));
                    ExpandBounds(bounds, ImVec2(base.x + radius + strokePad, base.y + radius + strokePad));
                    break;
                }
                case ShapeType::Line:
                case ShapeType::Triangle:
                case ShapeType::Polyline: {
                    for (const auto& point : primitive.Points) {
                        const ImVec2 world = Add(base, point);
                        ExpandBounds(bounds, ImVec2(world.x - strokePad, world.y - strokePad));
                        ExpandBounds(bounds, ImVec2(world.x + strokePad, world.y + strokePad));
                    }
                    break;
                }
                }
            }
        }

        inline ImVec4 GetBounds(const ShapeComponent& shapeComponent, const ImVec2& entityMin, const ImVec2& entityMax) {
            ImVec4 bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
            bool hasVisiblePrimitive = false;

            for (const auto& primitive : shapeComponent.Shapes) {
                if (!primitive.Visible) {
                    continue;
                }
                hasVisiblePrimitive = true;
                detail::ExpandBoundsForPrimitive(bounds, primitive, entityMin, entityMax);
            }

            if (!hasVisiblePrimitive) {
                return ImVec4(entityMin.x, entityMin.y, entityMax.x, entityMax.y);
            }
            return bounds;
        }

        inline bool HitTest(const ShapeComponent& shapeComponent, const ImVec2& point, const ImVec2& entityMin, const ImVec2& entityMax) {
            if (!shapeComponent.Enabled || !shapeComponent.UseForHitTest) {
                return false;
            }

            bool testedAny = false;
            for (const auto& primitive : shapeComponent.Shapes) {
                if (!primitive.Visible) {
                    continue;
                }

                testedAny = true;
                const ImVec2 base = detail::Add(entityMin, primitive.Offset);
                const ImVec2 size = detail::ResolveSize(primitive, entityMin, entityMax);

                switch (primitive.Type) {
                case ShapeType::Rect: {
                    const ImVec2 min = base;
                    const ImVec2 max = ImVec2(base.x + size.x, base.y + size.y);
                    if (point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y) {
                        return true;
                    }
                    break;
                }
                case ShapeType::Circle: {
                    const float radius = primitive.Radius > 0.0f ? primitive.Radius : (std::min)(size.x, size.y) * 0.5f;
                    const float dx = point.x - base.x;
                    const float dy = point.y - base.y;
                    if ((dx * dx + dy * dy) <= radius * radius) {
                        return true;
                    }
                    break;
                }
                case ShapeType::Line: {
                    if (primitive.Points.size() >= 2) {
                        const ImVec2 a = detail::Add(base, primitive.Points[0]);
                        const ImVec2 b = detail::Add(base, primitive.Points[1]);
                        const float thickness = primitive.StrokeThickness > 0.0f ? primitive.StrokeThickness : 1.0f;
                        if (detail::DistancePointToSegment(point, a, b) <= (thickness * 0.5f + 2.0f)) {
                            return true;
                        }
                    }
                    break;
                }
                case ShapeType::Triangle: {
                    if (primitive.Points.size() >= 3) {
                        const ImVec2 a = detail::Add(base, primitive.Points[0]);
                        const ImVec2 b = detail::Add(base, primitive.Points[1]);
                        const ImVec2 c = detail::Add(base, primitive.Points[2]);
                        if (detail::PointInTriangle(point, a, b, c)) {
                            return true;
                        }
                    }
                    break;
                }
                case ShapeType::Polyline: {
                    if (primitive.Points.size() >= 2) {
                        const float thickness = primitive.StrokeThickness > 0.0f ? primitive.StrokeThickness : 1.0f;
                        for (size_t i = 1; i < primitive.Points.size(); ++i) {
                            const ImVec2 a = detail::Add(base, primitive.Points[i - 1]);
                            const ImVec2 b = detail::Add(base, primitive.Points[i]);
                            if (detail::DistancePointToSegment(point, a, b) <= (thickness * 0.5f + 2.0f)) {
                                return true;
                            }
                        }
                    }
                    break;
                }
                }
            }

            if (!testedAny) {
                return (point.x >= entityMin.x && point.x <= entityMax.x && point.y >= entityMin.y && point.y <= entityMax.y);
            }
            return false;
        }

        inline void Draw(entt::registry& registry, entt::entity entity, ImDrawList* drawList, const ImVec2& entityMin, const ImVec2& entityMax, bool drawBehindContent) {
            auto* shapeComponent = registry.try_get<ShapeComponent>(entity);
            if (!shapeComponent || !shapeComponent->Enabled || shapeComponent->DrawBehindContent != drawBehindContent || !drawList) {
                return;
            }

            if (shapeComponent->ClipToEntity) {
                drawList->PushClipRect(entityMin, entityMax, true);
            }

            for (const auto& primitive : shapeComponent->Shapes) {
                if (!primitive.Visible) {
                    continue;
                }

                const ImVec2 base = detail::Add(entityMin, primitive.Offset);
                const ImVec2 size = detail::ResolveSize(primitive, entityMin, entityMax);

                switch (primitive.Type) {
                case ShapeType::Rect: {
                    const ImVec2 max = ImVec2(base.x + size.x, base.y + size.y);
                    if (primitive.Filled) {
                        drawList->AddRectFilled(base, max, primitive.FillColor, primitive.Rounding, primitive.RoundingFlags);
                    }
                    if (primitive.StrokeEnabled) {
                        drawList->AddRect(base, max, primitive.StrokeColor, primitive.Rounding, primitive.RoundingFlags, primitive.StrokeThickness);
                    }
                    break;
                }
                case ShapeType::Circle: {
                    const float radius = primitive.Radius > 0.0f ? primitive.Radius : (std::min)(size.x, size.y) * 0.5f;
                    if (primitive.Filled) {
                        drawList->AddCircleFilled(base, radius, primitive.FillColor, 24);
                    }
                    if (primitive.StrokeEnabled) {
                        drawList->AddCircle(base, radius, primitive.StrokeColor, 24, primitive.StrokeThickness);
                    }
                    break;
                }
                case ShapeType::Line: {
                    if (primitive.Points.size() >= 2) {
                        const ImVec2 a = detail::Add(base, primitive.Points[0]);
                        const ImVec2 b = detail::Add(base, primitive.Points[1]);
                        drawList->AddLine(a, b, primitive.StrokeEnabled ? primitive.StrokeColor : primitive.FillColor,
                            primitive.StrokeEnabled ? primitive.StrokeThickness : 1.0f);
                    }
                    break;
                }
                case ShapeType::Triangle: {
                    if (primitive.Points.size() >= 3) {
                        const ImVec2 a = detail::Add(base, primitive.Points[0]);
                        const ImVec2 b = detail::Add(base, primitive.Points[1]);
                        const ImVec2 c = detail::Add(base, primitive.Points[2]);
                        if (primitive.Filled) {
                            drawList->AddTriangleFilled(a, b, c, primitive.FillColor);
                        }
                        if (primitive.StrokeEnabled) {
                            drawList->AddTriangle(a, b, c, primitive.StrokeColor, primitive.StrokeThickness);
                        }
                    }
                    break;
                }
                case ShapeType::Polyline: {
                    if (primitive.Points.size() >= 2) {
                        std::vector<ImVec2> points;
                        points.reserve(primitive.Points.size());
                        for (const auto& point : primitive.Points) {
                            points.push_back(detail::Add(base, point));
                        }
                        drawList->AddPolyline(points.data(), static_cast<int>(points.size()),
                            primitive.StrokeEnabled ? primitive.StrokeColor : primitive.FillColor,
                            0, primitive.StrokeEnabled ? primitive.StrokeThickness : 1.0f);
                    }
                    break;
                }
                }
            }

            if (shapeComponent->OnDrawOverride) {
                shapeComponent->OnDrawOverride(registry, entity, drawList, entityMin, entityMax);
            }

            if (shapeComponent->ClipToEntity) {
                drawList->PopClipRect();
            }
        }
    } // namespace ShapeSystem
} // namespace RenderUtils
