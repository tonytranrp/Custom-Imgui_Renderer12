#include "UIRenderer.hpp"
#include <unordered_map>
#include <string>

// UIComponents.hpp is already included in UIRenderer.hpp, which now includes the sub-files.

namespace RenderUtils {

    bool UIRenderer::DebugMode = false;

    // --- Internal Layout Helper ---
    namespace {
        struct RenderSpan {
            std::string text;
            ImU32 color;
            bool bold;
            ImVec2 size;
        };

        struct RenderLine {
            std::vector<RenderSpan> spans;
            float width = 0.0f;
            float height = 0.0f;
        };

        std::vector<RenderLine> CalculateLayout(const TextComponent& textComp, float availableWidth) {
            std::vector<RenderLine> lines;
            
            // Ensure we have a font context
            if (!ImGui::GetCurrentContext()) return lines;

            auto MeasureText = [](const std::string& str, bool bold) -> ImVec2 {
                // For now, bold measurement is same as normal. 
                // If using real bold font, we'd push font here.
                return ImGui::CalcTextSize(str.c_str());
            };

            // Flatten spans/rawtext
            std::vector<RenderSpan> atoms;
            if (!textComp.Spans.empty()) {
                 for (const auto& span : textComp.Spans) {
                    bool isBold = HasStyle(span.Style, TextStyle::Bold);
                    atoms.push_back({ span.Text, span.Color, isBold, MeasureText(span.Text, isBold) });
                }
            } else {
                 atoms.push_back({ textComp.RawText, textComp.Color, false, MeasureText(textComp.RawText, false) });
            }

            float lineHeight = ImGui::GetTextLineHeight() * textComp.LineHeight;

            RenderLine currentLine;
            currentLine.height = lineHeight;

            for (const auto& atom : atoms) {
                 // Split atom into words
                 std::string remainingText = atom.text;
                 size_t pos = 0;
                 while ((pos = remainingText.find(' ')) != std::string::npos) {
                      std::string word = remainingText.substr(0, pos + 1); // include space
                      ImVec2 wordSize = MeasureText(word, atom.bold);

                      if (textComp.Wrap() && currentLine.width + wordSize.x > availableWidth && currentLine.width > 0) {
                          lines.push_back(currentLine);
                          currentLine = RenderLine();
                          currentLine.height = lineHeight;
                      }
                      currentLine.spans.push_back({ word, atom.color, atom.bold, wordSize });
                      currentLine.width += wordSize.x;
                      remainingText.erase(0, pos + 1);
                 }
                 if (!remainingText.empty()) {
                      ImVec2 wordSize = MeasureText(remainingText, atom.bold);
                      if (textComp.Wrap() && currentLine.width + wordSize.x > availableWidth && currentLine.width > 0) {
                          lines.push_back(currentLine);
                          currentLine = RenderLine();
                          currentLine.height = lineHeight;
                      }
                      currentLine.spans.push_back({ remainingText, atom.color, atom.bold, wordSize });
                      currentLine.width += wordSize.x;
                 }
            }
            if (!currentLine.spans.empty()) lines.push_back(currentLine);

            return lines;
        }
    }
    // ------------------------------

    void UIRenderer::Init() {
        // No special initialization needed for now, but good for future expansion (e.g., loading fonts)
    }

    void UIRenderer::ResolveDepth(entt::registry& registry) {
        // 1. Build Name Cache for fast lookup to avoid O(N^2) in finding targets
        std::unordered_map<std::string, entt::entity> nameToEntity;
        auto containerView = registry.view<ContainerComponent>();
        for(auto entity : containerView) {
            const auto& cc = containerView.get<ContainerComponent>(entity);
            if(cc.Name) {
                nameToEntity[cc.Name] = entity;
            }
        }

        // 2. Iterative Resolution for Dependency Chains (A > B > C)
        bool changed = true;
        int max_iterations = 10; // Sufficient for most UI hierarchies
        int iteration = 0;

        auto view = registry.view<DrawAboveComponent, StyleComponent>();
        
        while (changed && iteration < max_iterations) {
            changed = false;
            iteration++;

            view.each([&](const auto entity, const auto& drawAbove, auto& style) {
                if (drawAbove.TargetEntityName && drawAbove.TargetEntityName[0] != '\0') {
                    // Fast Lookup
                    auto it = nameToEntity.find(drawAbove.TargetEntityName);
                    if (it != nameToEntity.end()) {
                        entt::entity target = it->second;
                        
                        // Check if target has style
                        if (registry.valid(target) && registry.all_of<StyleComponent>(target)) {
                            const auto& targetStyle = registry.get<StyleComponent>(target);
                            
                            // Enforce Z-Index Constraint
                            if (style.ZIndexInt <= targetStyle.ZIndexInt) {
                                style.ZIndexInt = targetStyle.ZIndexInt + 1;
                                changed = true; // A change occurred, we might need another pass for dependents
                            }
                        }
                    }
                }
            });
        }
    }

    void UIRenderer::UpdateInput(entt::registry& registry) {
        ImVec2 mousePos = ImGui::GetMousePos();
        bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);

        // 1. Sort by Z-Index (Descending) for Input Capture
        registry.sort<StyleComponent>([](const auto& lhs, const auto& rhs) {
            return lhs.ZIndexInt > rhs.ZIndexInt; // DESCENDING
        });

        auto view = registry.view<StyleComponent, TransformComponent, InputStateComponent>();
        view.use<StyleComponent>(); // Iterate in Z-Index Descending order

        bool inputCaptured = false;

        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& inputState = view.get<InputStateComponent>(entity);
            
            // Check Dragging State first (maintain drag even if mouse moves fast)
            bool isDragging = false;
            if (registry.any_of<DraggableComponent>(entity)) {
                isDragging = registry.get<DraggableComponent>(entity).IsDragging;
            }

            if (isDragging) {
                inputCaptured = true; // Dragging consumes input
            }

            // Hit Test
            ImVec2 p_min = transform.Position;
            ImVec2 p_max = ImVec2(p_min.x + transform.Size.x, p_min.y + transform.Size.y);
            
            // Expanded Bounds Check (for Dropdowns)
            if (registry.any_of<ExpandComponent>(entity)) {
                const auto& expand = registry.get<ExpandComponent>(entity);
                if (expand.IsExpanded) {
                     if (expand.Direction == ExpandDirection::Down)
                         p_max.y += expand.CurrentHeight;
                     else
                         p_min.y -= expand.CurrentHeight;
                }
            }

            bool hovered = (mousePos.x >= p_min.x && mousePos.x <= p_max.x &&
                            mousePos.y >= p_min.y && mousePos.y <= p_max.y);

            // If input already captured by a higher element, we cannot be hovered
            if (inputCaptured) {
                inputState.IsHovered = false;
                inputState.IsClicked = false;
                continue;
            }

            if (hovered) {
                // If we are blocking input, we consume the event for layers below us.
                if (inputState.BlockInput) {
                     inputCaptured = true;
                }

                inputState.IsHovered = true;
                
                // Handle Scrolling
                if (registry.any_of<ScrollComponent>(entity)) {
                    auto& scroll = registry.get<ScrollComponent>(entity);
                    if (scroll.ContentHeight > scroll.ViewHeight) {
                        float wheel = ImGui::GetIO().MouseWheel;
                        if (wheel != 0) {
                            scroll.ScrollY -= wheel * scroll.Speed;
                            if (scroll.ScrollY < 0) scroll.ScrollY = 0;
                            float maxScroll = scroll.ContentHeight - scroll.ViewHeight;
                            if (scroll.ScrollY > maxScroll) scroll.ScrollY = maxScroll;
                        }
                    } else {
                        scroll.ScrollY = 0;
                    }
                }

                if (mouseClicked) {
                    inputState.IsClicked = true;
                } else if (!mouseDown) {
                    inputState.IsClicked = false;
                }
            } else {
                inputState.IsHovered = false;
                if (!mouseDown) inputState.IsClicked = false; 
            }

            // Handle TextInput Focus
            if (registry.any_of<TextInputComponent>(entity)) {
                auto& textInput = registry.get<TextInputComponent>(entity);
                if (mouseClicked) {
                    if (hovered) { 
                         textInput.IsFocused = true;
                    } else {
                         textInput.IsFocused = false;
                    }
                }
            }
            
            // Handle Slider Dragging State
            if (registry.any_of<SliderComponent>(entity)) {
                auto& slider = registry.get<SliderComponent>(entity);
                if (hovered && mouseClicked) {
                    slider.IsDragging = true;
                }
                if (!mouseDown) {
                    slider.IsDragging = false;
                }
            }
        }

        // 2. Handle Dragging Logic
        auto dragView = registry.view<TransformComponent, DraggableComponent, InputStateComponent>();
        
        for (auto entity : dragView) {
            auto& transform = dragView.get<TransformComponent>(entity);
            auto& draggable = dragView.get<DraggableComponent>(entity);
            const auto& inputState = dragView.get<InputStateComponent>(entity);

            // Locked Check
            if (registry.any_of<LockedComponent>(entity)) {
                if (registry.get<LockedComponent>(entity).Locked) {
                    draggable.IsDragging = false;
                    continue;
                }
            }

            bool draggingAllowed = (draggable.Mode != DragMode::None) || DebugMode;
            if (!draggingAllowed) continue;

            // Start Drag
            if (inputState.IsHovered && mouseClicked && !draggable.IsDragging) {
                draggable.IsDragging = true;
                draggable.DragOffset = ImVec2(mousePos.x - transform.Position.x, mousePos.y - transform.Position.y);
            }

            // Continue Drag
            if (draggable.IsDragging) {
                if (mouseDown) {
                    ImVec2 newPos = ImVec2(mousePos.x - draggable.DragOffset.x, mousePos.y - draggable.DragOffset.y);
                    
                    if (draggable.Mode == DragMode::HorizontalOnly) {
                        newPos.y = transform.Position.y; // Lock Y
                    } else if (draggable.Mode == DragMode::VerticalOnly) {
                        newPos.x = transform.Position.x; // Lock X
                    }

                    // Constraint Logic
                    if (draggable.Constraint == DragConstraint::Parent && registry.any_of<ParentComponent>(entity)) {
                        auto& parentComp = registry.get<ParentComponent>(entity);
                        if (registry.valid(parentComp.ParentEntity) && registry.all_of<TransformComponent>(parentComp.ParentEntity)) {
                            const auto& parentTrans = registry.get<TransformComponent>(parentComp.ParentEntity);
                            
                            float minX = parentTrans.Position.x;
                            float minY = parentTrans.Position.y;
                            float maxX = parentTrans.Position.x + parentTrans.Size.x - transform.Size.x;
                            float maxY = parentTrans.Position.y + parentTrans.Size.y - transform.Size.y;

                            bool precisionConstraintApplied = false;

                            if (registry.any_of<TextComponent>(entity)) {
                                const auto& tc = registry.get<TextComponent>(entity);
                                if (tc.PrecisionMode()) { 
                                    const auto* cp = registry.try_get<ContainerComponent>(entity);
                                    std::vector<ImVec4> lines = CalculateTextLines(tc, transform, cp);
                                    
                                    if (!lines.empty()) {
                                        float vMinX = FLT_MAX, vMaxX = -FLT_MAX, vMinY = FLT_MAX, vMaxY = -FLT_MAX;
                                        for(const auto& l : lines) {
                                            if(l.x < vMinX) vMinX = l.x;
                                            if(l.x + l.z > vMaxX) vMaxX = l.x + l.z;
                                            if(l.y < vMinY) vMinY = l.y;
                                            if(l.y + l.w > vMaxY) vMaxY = l.y + l.w;
                                        }
                                        
                                        float offMinX = vMinX - transform.Position.x;
                                        float offMaxX = vMaxX - transform.Position.x;
                                        float offMinY = vMinY - transform.Position.y;
                                        float offMaxY = vMaxY - transform.Position.y;

                                        minX = parentTrans.Position.x - offMinX;
                                        minY = parentTrans.Position.y - offMinY;
                                        maxX = (parentTrans.Position.x + parentTrans.Size.x) - offMaxX;
                                        maxY = (parentTrans.Position.y + parentTrans.Size.y) - offMaxY;
                                        
                                        precisionConstraintApplied = true;
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

                    // Collision Detection
                    if (registry.any_of<CollisionComponent>(entity)) {
                        ImVec2 p_old = transform.Position; 

                        std::vector<ImVec4> myRects;
                        bool iAmPrecision = false;
                        if (registry.any_of<TextComponent>(entity)) {
                            const auto& tc = registry.get<TextComponent>(entity);
                            if (tc.PrecisionMode()) {
                                iAmPrecision = true;
                                const auto* cp = registry.try_get<ContainerComponent>(entity);
                                TransformComponent tempTrans = transform;
                                tempTrans.Position = newPos;
                                myRects = CalculateTextLines(tc, tempTrans, cp);
                            }
                        }

                        if (!iAmPrecision) {
                            myRects.push_back(ImVec4(newPos.x, newPos.y, transform.Size.x, transform.Size.y));
                        }

                        auto collisionView = registry.view<TransformComponent, CollisionComponent>();
                        for (auto other : collisionView) {
                            if (other == entity) continue;
                            
                            // Check parent sharing
                            bool shareParent = false;
                            entt::entity parent1 = entt::null;
                            entt::entity parent2 = entt::null;
                            if (registry.any_of<ParentComponent>(entity)) parent1 = registry.get<ParentComponent>(entity).ParentEntity;
                            if (registry.any_of<ParentComponent>(other)) parent2 = registry.get<ParentComponent>(other).ParentEntity;
                            if (parent1 == parent2) shareParent = true;
                            if (!shareParent) continue;

                            const auto& otherTrans = collisionView.get<TransformComponent>(other);
                            
                            std::vector<ImVec4> otherRects;
                            bool otherIsPrecision = false;
                            if (registry.any_of<TextComponent>(other)) {
                                const auto& tc = registry.get<TextComponent>(other);
                                if (tc.PrecisionMode()) {
                                    otherIsPrecision = true;
                                    const auto* cp = registry.try_get<ContainerComponent>(other);
                                    otherRects = CalculateTextLines(tc, otherTrans, cp);
                                }
                            }
                            if (!otherIsPrecision) {
                                otherRects.push_back(ImVec4(otherTrans.Position.x, otherTrans.Position.y, otherTrans.Size.x, otherTrans.Size.y));
                            }
                            
                            for (const auto& myR : myRects) {
                                ImVec2 p_min = ImVec2(myR.x, myR.y);
                                ImVec2 p_max = ImVec2(myR.x + myR.z, myR.y + myR.w);
                                ImVec2 delta = ImVec2(newPos.x - p_old.x, newPos.y - p_old.y);
                                ImVec2 old_rect_min = ImVec2(p_min.x - delta.x, p_min.y - delta.y);
                                
                                for (const auto& otherR : otherRects) {
                                    ImVec2 o_min = ImVec2(otherR.x, otherR.y);
                                    ImVec2 o_max = ImVec2(otherR.x + otherR.z, otherR.y + otherR.w);

                                    if (p_min.x < o_max.x && p_max.x > o_min.x &&
                                        p_min.y < o_max.y && p_max.y > o_min.y) {
                                        
                                        float overlapLeft = (p_min.x + myR.z) - o_min.x;
                                        float overlapRight = (o_min.x + otherR.z) - p_min.x;
                                        float overlapTop = (p_min.y + myR.w) - o_min.y;
                                        float overlapBottom = (o_min.y + otherR.w) - p_min.y;
                                        
                                        bool wasLeft = (old_rect_min.x + myR.z) <= o_min.x + 1.0f;
                                        bool wasRight = old_rect_min.x >= o_max.x - 1.0f;
                                        bool wasAbove = (old_rect_min.y + myR.w) <= o_min.y + 1.0f;
                                        bool wasBelow = old_rect_min.y >= o_max.y - 1.0f;

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
                                        
                                        goto resolved_collision; 
                                    }
                                }
                            }
                            resolved_collision:;
                        }
                    }

                    transform.Position = newPos;

                    if (registry.any_of<ParentComponent>(entity)) {
                        auto& parentComp = registry.get<ParentComponent>(entity);
                        if (registry.valid(parentComp.ParentEntity) && registry.all_of<TransformComponent>(parentComp.ParentEntity)) {
                            const auto& parentTrans = registry.get<TransformComponent>(parentComp.ParentEntity);
                            parentComp.RelativeOffset = ImVec2(transform.Position.x - parentTrans.Position.x, 
                                                               transform.Position.y - parentTrans.Position.y);
                        }
                    }
                } else {
                    draggable.IsDragging = false;
                }
            }
        }

        // 3. Update Children Positions
        auto childView = registry.view<TransformComponent, ParentComponent>();
        for (auto entity : childView) {
            auto& transform = childView.get<TransformComponent>(entity);
            const auto& parent = childView.get<ParentComponent>(entity);

            if (registry.valid(parent.ParentEntity)) {
                if (registry.all_of<TransformComponent>(parent.ParentEntity)) {
                    const auto& parentTrans = registry.get<TransformComponent>(parent.ParentEntity);
                    transform.Position = ImVec2(parentTrans.Position.x + parent.RelativeOffset.x, 
                                              parentTrans.Position.y + parent.RelativeOffset.y);
                }
            }
        }
    }

    void UIRenderer::Render(entt::registry& registry) {
        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
        
        // 1. Sort Entities by ZIndex
        registry.sort<StyleComponent>([](const auto& lhs, const auto& rhs) {
            return lhs.ZIndexInt < rhs.ZIndexInt;
        });

        auto view = registry.view<const StyleComponent, const TransformComponent, const ContainerComponent>();
        view.use<const StyleComponent>(); 

        view.each([draw_list, &registry](const auto entity, const auto& style, const auto& transform, const auto& container) {
            if (!style.Visible) return;

            ImVec2 p_min = transform.Position;
            ImVec2 p_max = ImVec2(p_min.x + transform.Size.x, p_min.y + transform.Size.y);

            // Handle Transparency
            ImU32 bgColor = style.BackgroundColor;
            ImU32 borderColor = style.BorderColor;
            
            if (registry.any_of<TransparencyComponent>(entity)) {
                float alpha = registry.get<TransparencyComponent>(entity).Alpha;
                if (alpha < 1.0f) {
                    auto ApplyAlpha = [](ImU32 col, float a) -> ImU32 {
                        int r = (col >> 0) & 0xFF;
                        int g = (col >> 8) & 0xFF;
                        int b = (col >> 16) & 0xFF;
                        int original_a = (col >> 24) & 0xFF;
                        int new_a = static_cast<int>(original_a * a);
                        return IM_COL32(r, g, b, new_a);
                    };
                    bgColor = ApplyAlpha(bgColor, alpha);
                    borderColor = ApplyAlpha(borderColor, alpha);
                }
            }

            // Handle Clipping (Push)
            bool pushedClip = false;
            if (registry.any_of<ClipComponent>(entity)) {
                const auto& clip = registry.get<ClipComponent>(entity);
                if (clip.ClipChildren) {
                    draw_list->PushClipRect(p_min, p_max, true);
                    pushedClip = true;
                }
            }

            // Draw Background
            if ((bgColor & IM_COL32_A_MASK) != 0) {
                draw_list->AddRectFilled(p_min, p_max, bgColor, style.Rounding, style.RoundingFlags);
            }

            // Draw Custom Header for Windows
            if (container.Type == ContainerType::Window) {
                float headerHeight = 30.0f;
                ImVec2 header_max = ImVec2(p_max.x, p_min.y + headerHeight);
                ImDrawFlags headerRounding = style.RoundingFlags & ImDrawFlags_RoundCornersTop;
                draw_list->AddRectFilled(p_min, header_max, IM_COL32(40, 40, 50, 255), style.Rounding, headerRounding);
                if (container.Name) {
                    draw_list->AddText(ImVec2(p_min.x + 10, p_min.y + 7), IM_COL32(255, 255, 255, 255), container.Name);
                }
                draw_list->AddLine(ImVec2(p_min.x, header_max.y), ImVec2(p_max.x, header_max.y), IM_COL32(0, 0, 0, 100));
            }

            // Draw Border
            if (style.BorderSize > 0.0f && (borderColor & IM_COL32_A_MASK) != 0) {
                draw_list->AddRect(p_min, p_max, borderColor, style.Rounding, style.RoundingFlags, style.BorderSize);
            }

            // Debug Rendering
            if (DebugMode) {
                bool isPrecisionText = false;
                if (registry.any_of<TextComponent>(entity)) {
                     if (registry.get<TextComponent>(entity).PrecisionMode()) isPrecisionText = true; 
                }

                if (!isPrecisionText) {
                    draw_list->AddRect(p_min, p_max, IM_COL32(0, 255, 0, 255), 0.0f, 0, 1.0f);
                } else {
                    // Precision Mode Debug: Draw bounds for each line of text
                    const auto& text = registry.get<TextComponent>(entity);
                    auto lines = CalculateTextLines(text, transform, &container);
                    for (const auto& line : lines) {
                        // Draw yellow bounding box for each text line
                        draw_list->AddRect(ImVec2(line.x, line.y), ImVec2(line.x + line.z, line.y + line.w), IM_COL32(255, 255, 0, 255));
                    }
                    // Also draw the container name if present
                    if (container.Name) {
                        draw_list->AddText(ImVec2(p_min.x, p_min.y - 15), IM_COL32(255, 255, 0, 255), container.Name);
                    }
                }
                
                if (container.Name && !isPrecisionText) {
                    draw_list->AddText(ImVec2(p_min.x, p_min.y - 15), IM_COL32(0, 255, 0, 255), container.Name);
                }
            }

            // Render Text Content (Rich Text Support)
            if (registry.any_of<TextComponent>(entity)) {
                const auto& textComp = registry.get<TextComponent>(entity);
                bool hasContent = (!textComp.RawText.empty() || !textComp.Spans.empty());

                if (hasContent) {
                    float scrollOffset = 0.0f;
                    if (registry.any_of<ScrollComponent>(entity)) {
                        scrollOffset = registry.get<ScrollComponent>(entity).ScrollY;
                    }

                    ImVec2 text_start_pos = ImVec2(p_min.x + 10.0f, p_min.y + (container.Type == ContainerType::Window ? 40.0f : 10.0f) - scrollOffset);
                    float availableWidth = transform.Size.x - 20.0f;
                    
                    if (textComp.Clip()) {
                        draw_list->PushClipRect(p_min, p_max, true);
                    }

                    auto DrawTextSpan = [&](const std::string& str, const ImVec2& pos, ImU32 col, bool bold) {
                        if (bold) {
                            draw_list->AddText(ImVec2(pos.x + 1, pos.y), col, str.c_str());
                            draw_list->AddText(pos, col, str.c_str());
                        } else {
                            draw_list->AddText(pos, col, str.c_str());
                        }
                    };

                    ImVec2 cursor = text_start_pos;
                    float startX = text_start_pos.x;
                    
                    // Use shared layout logic
                    std::vector<RenderLine> lines = CalculateLayout(textComp, availableWidth);

                    // Render Lines with Alignment
                    for (size_t i = 0; i < lines.size(); ++i) {
                        const auto& line = lines[i];
                        float xOffset = 0.0f;
                        float extraSpacing = 0.0f;

                        if (textComp.Alignment == TextAlign::Center) {
                            xOffset = (availableWidth - line.width) * 0.5f;
                        } else if (textComp.Alignment == TextAlign::Right) {
                            xOffset = availableWidth - line.width;
                        } else if (textComp.Alignment == TextAlign::Justify) {
                             // Only justify if not the last line
                             if (i < lines.size() - 1 && line.spans.size() > 1) {
                                 float totalExtra = availableWidth - line.width;
                                 if (totalExtra > 0) {
                                     extraSpacing = totalExtra / (float)(line.spans.size() - 1);
                                 }
                             }
                        }

                        float currentX = startX + xOffset;
                        for (const auto& span : line.spans) {
                            DrawTextSpan(span.text, ImVec2(currentX, cursor.y), span.color, span.bold);
                            currentX += span.size.x;
                            if (textComp.Alignment == TextAlign::Justify) {
                                currentX += extraSpacing;
                            }
                        }
                        cursor.y += line.height;
                    }

                    if (textComp.Clip()) {
                        draw_list->PopClipRect();
                    }
                }
            }

            // Custom Component
            if (registry.any_of<CustomComponent>(entity)) {
                const auto& custom = registry.get<CustomComponent>(entity);
                bool isHovered = false;
                bool isClicked = false;
                if (registry.all_of<InputStateComponent>(entity)) {
                    const auto& input = registry.get<InputStateComponent>(entity);
                    isHovered = input.IsHovered;
                    isClicked = input.IsClicked;
                }
                if (custom.OnRender) {
                    custom.OnRender(registry, entity, draw_list, p_min, p_max, isHovered, isClicked);
                }
            }

            // Slider
            if (registry.any_of<SliderComponent>(entity)) {
                auto& slider = registry.get<SliderComponent>(entity);
                if (slider.IsDragging) {
                    float mouseX = ImGui::GetMousePos().x;
                    float trackStart = p_min.x;
                    float trackWidth = p_max.x - p_min.x;
                    float normalized = (mouseX - trackStart) / trackWidth;
                    if (normalized < 0.0f) normalized = 0.0f;
                    if (normalized > 1.0f) normalized = 1.0f;
                    float newValue = slider.Min + normalized * (slider.Max - slider.Min);
                    if (newValue != slider.Value) {
                        slider.Value = newValue;
                        if (slider.OnChange) slider.OnChange(slider.Value);
                    }
                }
                float trackH = 4.0f;
                float trackY = p_min.y + (p_max.y - p_min.y) * 0.5f - trackH * 0.5f;
                draw_list->AddRectFilled(ImVec2(p_min.x, trackY), ImVec2(p_max.x, trackY + trackH), IM_COL32(80, 80, 90, 255), 2.0f);
                float fillWidth = (slider.Value - slider.Min) / (slider.Max - slider.Min) * (p_max.x - p_min.x);
                draw_list->AddRectFilled(ImVec2(p_min.x, trackY), ImVec2(p_min.x + fillWidth, trackY + trackH), IM_COL32(100, 180, 255, 255), 2.0f);
                float knobX = p_min.x + fillWidth;
                draw_list->AddCircleFilled(ImVec2(knobX, trackY + trackH * 0.5f), 8.0f, IM_COL32(255, 255, 255, 255));
                if (registry.any_of<InputStateComponent>(entity) && registry.get<InputStateComponent>(entity).IsHovered) {
                     draw_list->AddCircle(ImVec2(knobX, trackY + trackH * 0.5f), 9.0f, IM_COL32(200, 200, 255, 100), 12, 2.0f);
                }
            }

            // TextInput
            if (registry.any_of<TextInputComponent>(entity)) {
                auto& textInput = registry.get<TextInputComponent>(entity);
                bool focused = textInput.IsFocused;
                ImU32 bgCol = focused ? IM_COL32(50, 50, 60, 255) : IM_COL32(40, 40, 45, 255);
                draw_list->AddRectFilled(p_min, p_max, bgCol, 4.0f);
                draw_list->AddRect(p_min, p_max, focused ? IM_COL32(100, 150, 255, 255) : IM_COL32(80, 80, 90, 255), 4.0f);
                draw_list->PushClipRect(ImVec2(p_min.x + 4, p_min.y), ImVec2(p_max.x - 4, p_max.y), true);
                const char* displayStr = textInput.Buffer.empty() ? textInput.Placeholder.c_str() : textInput.Buffer.c_str();
                ImU32 textCol = textInput.Buffer.empty() ? IM_COL32(150, 150, 150, 255) : IM_COL32(255, 255, 255, 255);
                float textH = ImGui::GetFontSize();
                float textY = p_min.y + (p_max.y - p_min.y) * 0.5f - textH * 0.5f;
                ImVec2 textPos = ImVec2(p_min.x + 5, textY);
                draw_list->AddText(textPos, textCol, displayStr);
                if (focused && (int(ImGui::GetTime() * 2) % 2 == 0)) {
                    ImVec2 textSize = ImGui::CalcTextSize(textInput.Buffer.c_str());
                    float cursorX = textPos.x + textSize.x;
                    draw_list->AddLine(ImVec2(cursorX, p_min.y + 4), ImVec2(cursorX, p_max.y - 4), IM_COL32(255, 255, 255, 255));
                }
                draw_list->PopClipRect();
            }

            // Scrollbar
            if (registry.any_of<ScrollComponent>(entity)) {
                const auto& scroll = registry.get<ScrollComponent>(entity);
                if (scroll.ShowScrollbar && scroll.ContentHeight > scroll.ViewHeight) {
                    ImVec2 trackMin, trackMax;
                    bool draw = true;
                    trackMin = ImVec2(p_max.x - 8, p_min.y);
                    trackMax = ImVec2(p_max.x - 2, p_max.y);
                    if (registry.any_of<ExpandComponent>(entity)) {
                          const auto& expand = registry.get<ExpandComponent>(entity);
                          if (expand.IsExpanded && expand.CurrentHeight > 0) {
                              trackMin = ImVec2(p_max.x - 8, p_max.y);
                              trackMax = ImVec2(p_max.x - 2, p_max.y + expand.CurrentHeight);
                          } else {
                              draw = false;
                          }
                     }
                     if (draw) {
                        draw_list->AddRectFilled(trackMin, trackMax, IM_COL32(30, 30, 35, 200), 4.0f);
                        float trackH = trackMax.y - trackMin.y;
                        if (trackH > 0) {
                            float thumbH = trackH * (scroll.ViewHeight / scroll.ContentHeight);
                            if (thumbH < 15) thumbH = 15;
                            if (thumbH > trackH) thumbH = trackH;
                            float maxScroll = scroll.ContentHeight - scroll.ViewHeight;
                            float ratio = (maxScroll > 0) ? (scroll.ScrollY / maxScroll) : 0;
                            if (ratio < 0) ratio = 0; if (ratio > 1) ratio = 1;
                            float thumbY = trackMin.y + ratio * (trackH - thumbH);
                            draw_list->AddRectFilled(ImVec2(trackMin.x + 1, thumbY + 1), ImVec2(trackMax.x - 1, thumbY + thumbH - 1), IM_COL32(100, 100, 120, 255), 4.0f);
                        }
                     }
                }
            }

            if (pushedClip) {
                draw_list->PopClipRect();
            }
        });
    }

    entt::entity UIRenderer::FindEntityByName(entt::registry& registry, const char* name) {
        auto view = registry.view<ContainerComponent>();
        for (auto entity : view) {
            const auto& container = view.get<ContainerComponent>(entity);
            if (container.Name && strcmp(container.Name, name) == 0) {
                return entity;
            }
        }
        return entt::null;
    }

    std::vector<ImVec4> UIRenderer::CalculateTextLines(const TextComponent& text, const TransformComponent& transform, const ContainerComponent* container) {
        std::vector<ImVec4> rects;
        
        float availableWidth = transform.Size.x - 20.0f; // Padding
        std::vector<RenderLine> lines = CalculateLayout(text, availableWidth);

        float startX = transform.Position.x + 10.0f;
        float startY = transform.Position.y + ((container && container->Type == ContainerType::Window) ? 40.0f : 10.0f);
        
        float cursorY = startY;

        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& line = lines[i];
            float xOffset = 0.0f;
            float lineWidth = line.width;
            
            // Calculate alignment offset
            if (text.Alignment == TextAlign::Center) {
                xOffset = (availableWidth - line.width) * 0.5f;
            } else if (text.Alignment == TextAlign::Right) {
                xOffset = availableWidth - line.width;
            } else if (text.Alignment == TextAlign::Justify) {
                if (i < lines.size() - 1 && line.spans.size() > 1) {
                    if (availableWidth > line.width) {
                        lineWidth = availableWidth;
                    }
                }
            }
            
            // We return the bounding box of the whole line
            // x, y, width, height
            rects.push_back(ImVec4(startX + xOffset, cursorY, lineWidth, line.height));
            
            cursorY += line.height;
        }
        
        return rects;
    }

    entt::entity UIRenderer::CreateContainer(entt::registry& registry, const char* name, 
                                             const ImVec2& pos, const ImVec2& size, ImU32 color) {
        auto entity = registry.create();
        
        registry.emplace<TransformComponent>(entity, pos, size);
        registry.emplace<StyleComponent>(entity, color, IM_COL32(255, 255, 255, 255), 1.0f, 5.0f, true, ZOrder::Normal);
        registry.emplace<ContainerComponent>(entity, ContainerType::Panel, name);
        registry.emplace<InputStateComponent>(entity, false, false);
        registry.emplace<DraggableComponent>(entity, DragMode::None, DragConstraint::None);
        
        return entity;
    }

    entt::entity UIRenderer::CreateChildContainer(entt::registry& registry, entt::entity parent, const char* name,
                                                  const ImVec2& relativePos, const ImVec2& size,
                                                  ImU32 color) {
        auto entity = CreateContainer(registry, name, ImVec2(0,0), size, color); 
        
        auto& parentComp = registry.emplace<ParentComponent>(entity);
        parentComp.ParentEntity = parent;
        parentComp.RelativeOffset = relativePos;

        if (registry.all_of<StyleComponent>(parent)) {
            int parentZ = registry.get<StyleComponent>(parent).ZIndexInt;
            registry.get<StyleComponent>(entity).ZIndexInt = parentZ + 1;
        }

        return entity;
    }

}
