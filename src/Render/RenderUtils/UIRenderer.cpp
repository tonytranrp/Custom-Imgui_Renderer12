#include "UIRenderer.hpp"
#include "Dx12Init/Dx12Init.hpp"
#include "RustComponents/RustBridge.hpp"
#include <unordered_map>
#include <string>
#include <algorithm>
#include <cmath>

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
                 // Split atom into tokens (words and newlines)
                 std::string remainingText = atom.text;
                 size_t pos = 0;
                 
                 // Helper to push a word
                 auto PushWord = [&](std::string word) {
                      ImVec2 wordSize = MeasureText(word, atom.bold);

                      if (textComp.Wrap() && currentLine.width + wordSize.x > availableWidth && currentLine.width > 0) {
                          lines.push_back(currentLine);
                          currentLine = RenderLine();
                          currentLine.height = lineHeight;
                      }
                      currentLine.spans.push_back({ word, atom.color, atom.bold, wordSize });
                      currentLine.width += wordSize.x;
                      // Dynamic height adjustment (if font size varies or text is taller than line height)
                      if (wordSize.y > currentLine.height) {
                           currentLine.height = wordSize.y;
                      }
                 };

                 // Helper to force newline
                 auto ForceNewline = [&]() {
                      lines.push_back(currentLine);
                      currentLine = RenderLine();
                      currentLine.height = lineHeight;
                 };

                 while (!remainingText.empty()) {
                      size_t nextSpace = remainingText.find(' ');
                      size_t nextNewline = remainingText.find('\n');
                      
                      size_t splitPos = std::string::npos;
                      bool isNewline = false;

                      if (nextSpace != std::string::npos && nextNewline != std::string::npos) {
                          if (nextSpace < nextNewline) {
                              splitPos = nextSpace;
                          } else {
                              splitPos = nextNewline;
                              isNewline = true;
                          }
                      } else if (nextSpace != std::string::npos) {
                          splitPos = nextSpace;
                      } else if (nextNewline != std::string::npos) {
                          splitPos = nextNewline;
                          isNewline = true;
                      }

                      if (splitPos != std::string::npos) {
                           std::string token = remainingText.substr(0, splitPos);
                           if (isNewline) {
                               if (!token.empty()) PushWord(token); // Push word before newline
                               ForceNewline();
                               remainingText.erase(0, splitPos + 1); // Remove word and \n
                           } else {
                               // It's a space
                               token += ' '; // Include space in word
                               PushWord(token);
                               remainingText.erase(0, splitPos + 1);
                           }
                      } else {
                           // No more delimiters
                           PushWord(remainingText);
                           remainingText.clear();
                      }
                 }
            }
            if (!currentLine.spans.empty()) lines.push_back(currentLine);

            return lines;
        }
    }
    // ------------------------------

    int UIRenderer::s_CurrentTabId = 0;
    entt::entity UIRenderer::s_SelectedEntity = entt::null;
    entt::entity UIRenderer::s_HoveredDebugEntity = entt::null; // New: Best candidate for selection

    void UIRenderer::SetCurrentTab(int tabId) {
        s_CurrentTabId = tabId;
    }

    int UIRenderer::GetCurrentTab() {
        return s_CurrentTabId;
    }

    void UIRenderer::RenderInspector(entt::registry& registry) {
        if (!DebugMode) return;

        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        
        if (registry.valid(s_SelectedEntity)) {
            ImGui::Text("Entity ID: %d", (int)s_SelectedEntity);
            
            // --- Hierarchy Navigation ---
            if (ImGui::CollapsingHeader("Hierarchy", ImGuiTreeNodeFlags_DefaultOpen)) {
                // Parent Link
                if (registry.all_of<ParentComponent>(s_SelectedEntity)) {
                    entt::entity parent = registry.get<ParentComponent>(s_SelectedEntity).ParentEntity;
                    if (registry.valid(parent)) {
                         std::string pName = "Parent (ID: " + std::to_string((int)parent) + ")";
                         if (registry.all_of<ContainerComponent>(parent)) {
                             const char* n = registry.get<ContainerComponent>(parent).Name;
                             if (n) pName += " - " + std::string(n);
                         }
                         if (ImGui::Button(pName.c_str())) {
                             s_SelectedEntity = parent;
                         }
                    } else {
                        ImGui::TextDisabled("Parent: Invalid");
                    }
                } else {
                    ImGui::TextDisabled("No Parent (Root)");
                }

                ImGui::Separator();
                ImGui::Text("Children:");
                
                // Find Children (Slow linear search, but okay for debug inspector)
                bool hasChildren = false;
                auto view = registry.view<ParentComponent>();
                for(auto entity : view) {
                    if (view.get<ParentComponent>(entity).ParentEntity == s_SelectedEntity) {
                        hasChildren = true;
                        std::string cName = "ID: " + std::to_string((int)entity);
                        if (registry.all_of<ContainerComponent>(entity)) {
                            const char* n = registry.get<ContainerComponent>(entity).Name;
                            if (n) cName += " - " + std::string(n);
                        }
                        if (ImGui::Selectable(cName.c_str())) {
                            s_SelectedEntity = entity;
                        }
                    }
                }
                if (!hasChildren) ImGui::TextDisabled("No Children");
            }
            
            // 1. Container Component
            if (registry.all_of<ContainerComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Container", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& cc = registry.get<ContainerComponent>(s_SelectedEntity);
                    if (cc.Name) ImGui::Text("Name: %s", cc.Name);
                    ImGui::Text("Type: %d", (int)cc.Type);
                }
            }

            // 2. Transform Component
            if (registry.all_of<TransformComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& tc = registry.get<TransformComponent>(s_SelectedEntity);
                    ImGui::DragFloat2("Position", &tc.Position.x);
                    ImGui::DragFloat2("Size", &tc.Size.x);
                    ImGui::DragFloat("Rotation", &tc.Rotation);
                }
            }

            // 3. Style Component
            if (registry.all_of<StyleComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Style", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& sc = registry.get<StyleComponent>(s_SelectedEntity);
                    ImGui::Checkbox("Visible", &sc.Visible);
                    ImGui::Text("Calculated Visible: %s", sc.CalculatedVisible ? "True" : "False");
                    
                    ImVec4 bgCol = ImColor(sc.BackgroundColor);
                    if (ImGui::ColorEdit4("Background", (float*)&bgCol)) {
                        sc.BackgroundColor = ImColor(bgCol);
                    }
                    
                    ImVec4 borderCol = ImColor(sc.BorderColor);
                    if (ImGui::ColorEdit4("Border Color", (float*)&borderCol)) {
                        sc.BorderColor = ImColor(borderCol);
                    }
                    
                    ImGui::DragFloat("Border Size", &sc.BorderSize);
                    ImGui::DragFloat("Rounding", &sc.Rounding);
                    ImGui::DragInt("Z-Index", &sc.ZIndexInt);
                }
            }

            // 4. Text Component
            if (registry.all_of<TextComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& txt = registry.get<TextComponent>(s_SelectedEntity);
                    // Use a static buffer for editing text (simple version)
                    static char buf[256];
                    strncpy(buf, txt.RawText.c_str(), 255);
                    if (ImGui::InputText("Content", buf, 256)) {
                        txt.RawText = buf;
                    }
                    
                    ImVec4 txtCol = ImColor(txt.Color);
                    if (ImGui::ColorEdit4("Color", (float*)&txtCol)) {
                        txt.Color = ImColor(txtCol);
                    }
                    
                    ImGui::DragFloat("Line Height", &txt.LineHeight, 0.1f, 0.5f, 3.0f);
                    
                    const char* alignments[] = { "Left", "Center", "Right", "Justify" };
                    int align = (int)txt.Alignment;
                    if (ImGui::Combo("Align", &align, alignments, 4)) {
                        txt.Alignment = (TextAlign)align;
                    }
                    
                    // Spans Info
                    if (!txt.Spans.empty()) {
                        ImGui::Text("Spans: %d", (int)txt.Spans.size());
                        for(size_t i=0; i<txt.Spans.size(); i++) {
                            ImGui::BulletText("Span %d: %s", (int)i, txt.Spans[i].Text.c_str());
                        }
                    }
                }
            }
            
            // 6. Glow Component
            if (registry.all_of<GlowComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Glow", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& glow = registry.get<GlowComponent>(s_SelectedEntity);
                    ImGui::Checkbox("Enabled", &glow.Enabled);
                    ImGui::DragFloat("Radius", &glow.Radius, 1.0f, 0.0f, 100.0f);
                    ImGui::DragFloat("Intensity", &glow.Intensity, 0.01f, 0.0f, 5.0f);
                    ImGui::DragInt("Samples", &glow.Samples, 1, 1, 32);
                    
                    ImVec4 gCol = ImColor(glow.Color);
                    if (ImGui::ColorEdit4("Glow Color", (float*)&gCol)) {
                        glow.Color = ImColor(gCol);
                    }
                }
            }

            // 7. Input State
            if (registry.all_of<InputStateComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Input State", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& input = registry.get<InputStateComponent>(s_SelectedEntity);
                    ImGui::Text("Hovered: %s", input.IsHovered ? "Yes" : "No");
                    ImGui::Text("Clicked: %s", input.IsClicked ? "Yes" : "No");
                    ImGui::Checkbox("Block Input", &input.BlockInput);
                    ImGui::Text("ClipRect: %.1f, %.1f, %.1f, %.1f", input.ClipRect.x, input.ClipRect.y, input.ClipRect.z, input.ClipRect.w);
                }
            }

            // 8. Animation Component
            if (registry.all_of<AnimationComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Animations", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& anim = registry.get<AnimationComponent>(s_SelectedEntity);
                    ImGui::Text("Active Animations: %d", (int)anim.Animations.size());
                    for (size_t i = 0; i < anim.Animations.size(); i++) {
                        auto& a = anim.Animations[i];
                        ImGui::ProgressBar(a.Elapsed / a.Duration, ImVec2(-1, 0), "Progress");
                    }
                }
            }
            
            // 9. Custom Component
            if (registry.all_of<CustomComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Custom Component", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Text("Has Render Callback: Yes");
                }
            }
            
            // 10. Scroll Component
            if (registry.all_of<ScrollComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Scroll", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& scroll = registry.get<ScrollComponent>(s_SelectedEntity);
                    ImGui::DragFloat("Scroll Y", &scroll.ScrollY);
                    ImGui::Text("Content Height: %.1f", scroll.ContentHeight);
                    ImGui::Text("View Height: %.1f", scroll.ViewHeight);
                    ImGui::Checkbox("Show Scrollbar", &scroll.ShowScrollbar);
                }
            }

            // 11. Draggable Component (Add/Remove/Edit)
            if (ImGui::CollapsingHeader("Interaction", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool hasDraggable = registry.all_of<DraggableComponent>(s_SelectedEntity);
                if (ImGui::Checkbox("Is Draggable", &hasDraggable)) {
                    if (hasDraggable) {
                        if (!registry.all_of<DraggableComponent>(s_SelectedEntity)) {
                            registry.emplace<DraggableComponent>(s_SelectedEntity, DragMode::Free);
                        }
                    } else {
                        if (registry.all_of<DraggableComponent>(s_SelectedEntity)) {
                            registry.remove<DraggableComponent>(s_SelectedEntity);
                        }
                    }
                }

                if (registry.all_of<DraggableComponent>(s_SelectedEntity)) {
                    auto& drag = registry.get<DraggableComponent>(s_SelectedEntity);
                    
                    const char* modes[] = { "None", "Free", "Horizontal", "Vertical" };
                    int currentMode = (int)drag.Mode;
                    if (ImGui::Combo("Drag Mode", &currentMode, modes, 4)) {
                        drag.Mode = (DragMode)currentMode;
                    }

                    const char* constraints[] = { "None", "Parent", "Window", "Screen" };
                    int currentConst = (int)drag.Constraint;
                    if (ImGui::Combo("Constraint", &currentConst, constraints, 4)) {
                        drag.Constraint = (DragConstraint)currentConst;
                    }
                    
                    ImGui::Text("Dragging: %s", drag.IsDragging ? "Yes" : "No");
                }
            }

            // 12. Draw Above / Z-Order Override
            if (ImGui::CollapsingHeader("Z-Order / Layers", ImGuiTreeNodeFlags_DefaultOpen)) {
                 bool hasDrawAbove = registry.all_of<DrawAboveComponent>(s_SelectedEntity);
                 if (ImGui::Checkbox("Draw Above Another Entity", &hasDrawAbove)) {
                     if (hasDrawAbove) {
                         if (!registry.all_of<DrawAboveComponent>(s_SelectedEntity)) {
                             registry.emplace<DrawAboveComponent>(s_SelectedEntity, "");
                         }
                     } else {
                         if (registry.all_of<DrawAboveComponent>(s_SelectedEntity)) {
                             registry.remove<DrawAboveComponent>(s_SelectedEntity);
                         }
                     }
                 }

                 if (registry.all_of<DrawAboveComponent>(s_SelectedEntity)) {
                     auto& da = registry.get<DrawAboveComponent>(s_SelectedEntity);
                     
                     // Use ComboBox to select target entity from existing containers
                     const char* currentTarget = (da.TargetEntityName && strlen(da.TargetEntityName) > 0) ? da.TargetEntityName : "None";
                     if (ImGui::BeginCombo("Target Entity", currentTarget)) {
                         if (ImGui::Selectable("None", da.TargetEntityName == nullptr || strlen(da.TargetEntityName) == 0)) {
                             da.TargetEntityName = "";
                         }
                         
                         auto containerView = registry.view<ContainerComponent>();
                         for(auto entity : containerView) {
                             if (entity == s_SelectedEntity) continue; // Don't draw above self
                             
                             const auto& cc = containerView.get<ContainerComponent>(entity);
                             const char* name = cc.Name;
                             if (name && strlen(name) > 0) {
                                 bool isSelected = (da.TargetEntityName && strcmp(da.TargetEntityName, name) == 0);
                                 if (ImGui::Selectable(name, isSelected)) {
                                     da.TargetEntityName = name; // Warning: Assumes Name points to static/persistent string
                                 }
                                 if (isSelected) ImGui::SetItemDefaultFocus();
                             }
                         }
                         ImGui::EndCombo();
                     }
                 }
            }

        } else {
            ImGui::Text("No entity selected.");
            ImGui::Text("Click an element in the view to inspect.");
        }
        
        ImGui::End();
    }

    void UIRenderer::Init(entt::registry& registry) {

    }




    void UIRenderer::Update(entt::registry& registry, float deltaTime) {
        // 1. Tab Switching Logic
        {
            auto tabView = registry.view<TabSwitchComponent, StyleComponent>();
            for(auto entity : tabView) {
                auto& tab = tabView.get<TabSwitchComponent>(entity);
                auto& style = tabView.get<StyleComponent>(entity);
                
                bool shouldBeVisible = (tab.TargetTabId == s_CurrentTabId);
                
                if (shouldBeVisible && !style.Visible) {
                    // Just became visible - Animate In (Slide Down)
                    style.Visible = true;
                    if (registry.all_of<TransformComponent>(entity)) {
                        // Ensure TransparencyComponent exists
                        if (!registry.any_of<TransparencyComponent>(entity)) {
                            registry.emplace<TransparencyComponent>(entity, 0.0f);
                        }
                        
                        // Animate Alpha 0 -> 1
                        if (!registry.any_of<AnimationComponent>(entity)) {
                            registry.emplace<AnimationComponent>(entity);
                        }
                        auto& animComp = registry.get<AnimationComponent>(entity);
                        
                        Animation anim;
                        anim.Duration = 0.5f;
                        anim.Easing = EasingType::EaseOutCubic;
                        anim.StartVal.data = 0.0f;
                        anim.EndVal.data = 1.0f;
                        anim.Apply = [entity, &registry](const AnimationValue& val) {
                            if (registry.valid(entity))
                                registry.get<TransparencyComponent>(entity).Alpha = std::get<float>(val.data);
                        };
                        animComp.AddAnimation(anim);
                        
                        // Slide In from Top (-20 y to 0 y)
                        Animation animPos;
                        animPos.Duration = 0.5f;
                        animPos.Easing = EasingType::EaseOutCubic;
                        animPos.StartVal.data = ImVec2(0, -20);
                        animPos.EndVal.data = ImVec2(0, 0);
                        animPos.Apply = [entity, &registry](const AnimationValue& val) {
                            if (registry.valid(entity)) {
                                if (registry.all_of<ParentComponent>(entity)) {
                                    registry.get<ParentComponent>(entity).RelativeOffset = std::get<ImVec2>(val.data);
                                }
                            }
                        };
                        animComp.AddAnimation(animPos);
                    }
                } else if (!shouldBeVisible && style.Visible) {
                    style.Visible = false;
                }
            }
        }

        // 2. Tab Trigger Buttons Logic
        {
            auto btnView = registry.view<TabTriggerComponent, StyleComponent, InputStateComponent>();
            for (auto entity : btnView) {
                const auto& trigger = btnView.get<TabTriggerComponent>(entity);
                auto& style = btnView.get<StyleComponent>(entity);
                const auto& input = btnView.get<InputStateComponent>(entity);

                // Handle Click
                // Note: We check if clicked THIS frame.
                // InputState.IsClicked is true while mouse is down.
                // We want "Just Pressed".
                // Ideally InputState should have IsPressed vs IsHeld.
                // For now, let's use ImGui directly for logic or rely on CustomComponent logic previously used.
                // But wait, the user wants "library handles it".
                // If we use `UpdateInput`, we know if it's clicked.
                // Let's check if hovered and mouse released? Or mouse pressed.
                // ImGui::IsMouseClicked(0) is global.
                // If input.IsHovered && ImGui::IsMouseClicked(0), then trigger.
                if (input.IsHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    SetCurrentTab(trigger.TabId);
                }

                // Animate Color
                bool isActive = (trigger.TabId == s_CurrentTabId);
                ImU32 targetColor = isActive ? trigger.ActiveColor : trigger.InactiveColor;

                if (style.BackgroundColor != targetColor) {
                     if (!registry.any_of<AnimationComponent>(entity)) {
                         registry.emplace<AnimationComponent>(entity);
                     }
                     auto& animComp = registry.get<AnimationComponent>(entity);
                     
                     // Check if we already have a color animation targeting this value
                     bool alreadyAnimating = false;
                     for(const auto& a : animComp.Animations) {
                         if (std::holds_alternative<ImU32>(a.EndVal.data)) {
                             if (std::get<ImU32>(a.EndVal.data) == targetColor) {
                                 alreadyAnimating = true;
                                 break;
                             }
                         }
                     }
                     
                     if (!alreadyAnimating) {
                         Animation anim;
                         anim.Duration = 0.2f;
                         anim.Easing = EasingType::EaseOutQuad;
                         anim.StartVal.data = style.BackgroundColor;
                         anim.EndVal.data = targetColor;
                         anim.Apply = [entity, &registry](const AnimationValue& val) {
                             if (registry.valid(entity))
                                 registry.get<StyleComponent>(entity).BackgroundColor = std::get<ImU32>(val.data);
                         };
                         animComp.AddAnimation(anim);
                     }
                }
            }
        }

        // 3. Standard Updates
        UpdateAnimations(registry, deltaTime);
        ResolveTransforms(registry);
        ResolveDepth(registry);
        UpdateInput(registry);
        UpdateText(registry);
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

    void UIRenderer::ResolveTransforms(entt::registry& registry) {
        // Build Hierarchy Map
        std::unordered_map<entt::entity, std::vector<entt::entity>> childrenMap;
        std::vector<entt::entity> roots;

        auto view = registry.view<TransformComponent>();
        for (auto entity : view) {
            bool isRoot = true;
            if (registry.all_of<ParentComponent>(entity)) {
                const auto& parentComp = registry.get<ParentComponent>(entity);
                if (registry.valid(parentComp.ParentEntity) && registry.all_of<TransformComponent>(parentComp.ParentEntity)) {
                    childrenMap[parentComp.ParentEntity].push_back(entity);
                    isRoot = false;
                }
            }
            if (isRoot) {
                roots.push_back(entity);
            }
        }

        // Recursive Update Function
        std::function<void(entt::entity, ImVec2, int, ImVec4, bool)> UpdateRecursive = 
            [&](entt::entity entity, ImVec2 parentPos, int parentZ, ImVec4 parentClip, bool parentVisible) {
            
            auto& transform = registry.get<TransformComponent>(entity);
            
            // Apply Relative Offset
            if (registry.all_of<ParentComponent>(entity)) {
                const auto& pc = registry.get<ParentComponent>(entity);
                transform.Position = ImVec2(parentPos.x + pc.RelativeOffset.x, parentPos.y + pc.RelativeOffset.y);
            }

            // Calculate Visibility
            bool isVisible = parentVisible;
            if (registry.all_of<StyleComponent>(entity)) {
                auto& style = registry.get<StyleComponent>(entity);
                // Combine Parent Visibility with Self Visibility
                isVisible = isVisible && style.Visible;
                style.CalculatedVisible = isVisible;
                
                // Update Z-Index
                if (style.ZIndexInt <= parentZ) {
                    style.ZIndexInt = parentZ + 1;
                }
                parentZ = style.ZIndexInt;
            }

            // Calculate Current Clip Rect (Intersection of Parent Clip and Self Clip if enabled)
            if (registry.all_of<InputStateComponent>(entity)) {
                registry.get<InputStateComponent>(entity).ClipRect = parentClip;
            }

            ImVec4 nextClip = parentClip;
            if (registry.all_of<ClipComponent>(entity)) {
                const auto& clip = registry.get<ClipComponent>(entity);
                if (clip.ClipChildren) {
                    // Intersect parentClip with our bounds
                    ImVec2 myMin = transform.Position;
                    ImVec2 myMax = ImVec2(myMin.x + transform.Size.x, myMin.y + transform.Size.y);
                    
                    // Header compensation for Windows
                    if (registry.all_of<ContainerComponent>(entity) && 
                        registry.get<ContainerComponent>(entity).Type == ContainerType::Window) {
                        myMin.y += 30.0f;
                    }

                    nextClip.x = (parentClip.x > myMin.x) ? parentClip.x : myMin.x;
                    nextClip.y = (parentClip.y > myMin.y) ? parentClip.y : myMin.y;
                    nextClip.z = (parentClip.z < myMax.x) ? parentClip.z : myMax.x;
                    nextClip.w = (parentClip.w < myMax.y) ? parentClip.w : myMax.y;
                }
            }

            // Handle Scroll Offset for Children
            ImVec2 childBasePos = transform.Position;
            if (registry.all_of<ScrollComponent>(entity)) {
                const auto& scroll = registry.get<ScrollComponent>(entity);
                childBasePos.y -= scroll.ScrollY;
            }

            // Process Children
            if (childrenMap.find(entity) != childrenMap.end()) {
                for (auto child : childrenMap[entity]) {
                    UpdateRecursive(child, childBasePos, parentZ, nextClip, isVisible);
                }
            }
        };

        // Process all roots
        for (auto root : roots) {
            int rootZ = 0;
            bool rootVisible = true;
            if (registry.all_of<StyleComponent>(root)) {
                auto& s = registry.get<StyleComponent>(root);
                rootZ = s.ZIndexInt;
                rootVisible = s.Visible;
                s.CalculatedVisible = s.Visible; // Roots have no parent to hide them
            }
            // Roots are not clipped initially
            UpdateRecursive(root, ImVec2(0,0), rootZ - 1, ImVec4(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX), rootVisible); 
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
            auto& style = view.get<StyleComponent>(entity);

            if (!style.CalculatedVisible) {
                inputState.IsHovered = false;
                inputState.IsClicked = false;
                continue;
            }
            
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

            // Check against Calculated Clip Rect (from ResolveTransforms)
            // If the mouse is outside the visible area of the parent(s), we are not hovered.
            if (mousePos.x < inputState.ClipRect.x || mousePos.x > inputState.ClipRect.z ||
                mousePos.y < inputState.ClipRect.y || mousePos.y > inputState.ClipRect.w) {
                hovered = false;
            }

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
                    
                    // Update View Height based on current Transform Size
                    scroll.ViewHeight = transform.Size.y;

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
            // Debug Consistency: Allow dragging selected entity even if occluded by children, and ignore header constraint
            bool debugOverride = DebugMode && (entity == s_SelectedEntity);
            bool effectivelyHovered = inputState.IsHovered;

            if (debugOverride && !effectivelyHovered) {
                 // Raw Hit Test
                 if (mousePos.x >= transform.Position.x && mousePos.x <= transform.Position.x + transform.Size.x &&
                     mousePos.y >= transform.Position.y && mousePos.y <= transform.Position.y + transform.Size.y) {
                     effectivelyHovered = true;
                 }
            }

            if (effectivelyHovered && mouseClicked && !draggable.IsDragging) {
                // Constraint: Windows can only be dragged by their header (top 30px)
                bool canDrag = true;
                
                // Only enforce header constraint if NOT in Debug Mode
                if (!DebugMode) {
                    if (registry.all_of<ContainerComponent>(entity) && registry.get<ContainerComponent>(entity).Type == ContainerType::Window) {
                        if (mousePos.y > transform.Position.y + 30.0f) {
                            canDrag = false;
                        }
                    }
                }
                
                if (canDrag) {
                    draggable.IsDragging = true;
                    draggable.DragOffset = ImVec2(mousePos.x - transform.Position.x, mousePos.y - transform.Position.y);
                }
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
                    
                    float scrollOffset = 0.0f;
                    if (registry.all_of<ScrollComponent>(parent.ParentEntity)) {
                        scrollOffset = registry.get<ScrollComponent>(parent.ParentEntity).ScrollY;
                    }

                    transform.Position = ImVec2(parentTrans.Position.x + parent.RelativeOffset.x, 
                                              parentTrans.Position.y + parent.RelativeOffset.y - scrollOffset);
                }
            }
        }
    }

    void UIRenderer::UpdateText(entt::registry& registry) {
        auto view = registry.view<TextInputComponent, InputStateComponent>();
        for (auto entity : view) {
            auto& textInput = view.get<TextInputComponent>(entity);

            if (textInput.IsFocused) {
                ImGuiIO& io = ImGui::GetIO();

                // Safety: Clamp CursorPos
                if (textInput.CursorPos < 0) textInput.CursorPos = 0;
                if (textInput.CursorPos > textInput.Buffer.length()) textInput.CursorPos = (int)textInput.Buffer.length();
                
                // --- Cursor Movement ---
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                    if (textInput.CursorPos > 0) textInput.CursorPos--;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                    if (textInput.CursorPos < textInput.Buffer.length()) textInput.CursorPos++;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
                    textInput.CursorPos = 0;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_End)) {
                    textInput.CursorPos = (int)textInput.Buffer.length();
                }

                // --- Editing ---

                // Handle Backspace (Delete character BEFORE cursor)
                // Note: We use Repeat mode for Backspace for better feel
                if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
                    if (textInput.CursorPos > 0 && !textInput.Buffer.empty()) {
                        textInput.Buffer.erase(textInput.CursorPos - 1, 1);
                        textInput.CursorPos--;
                        if (textInput.OnChange) textInput.OnChange(textInput.Buffer);
                    }
                }

                // Handle Delete (Delete character AFTER cursor)
                if (ImGui::IsKeyPressed(ImGuiKey_Delete, true)) {
                    if (textInput.CursorPos < textInput.Buffer.length()) {
                        textInput.Buffer.erase(textInput.CursorPos, 1);
                        if (textInput.OnChange) textInput.OnChange(textInput.Buffer);
                    }
                }

                // Handle Paste (Ctrl+V)
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
                    const char* clipboard = ImGui::GetClipboardText();
                    if (clipboard) {
                        std::string clipStr = clipboard;
                        if (textInput.Buffer.length() + clipStr.length() <= textInput.MaxLength) {
                            textInput.Buffer.insert(textInput.CursorPos, clipStr);
                            textInput.CursorPos += (int)clipStr.length();
                            if (textInput.OnChange) textInput.OnChange(textInput.Buffer);
                        }
                    }
                }

                // Handle Character Input
                for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
                    char c = (char)io.InputQueueCharacters[i];
                    if (c > 0 && c < 0x80) { // ASCII only for now
                         // Ignore control characters (like Backspace which is handled separately, or Enter)
                         if (c >= 32) { 
                             if (textInput.Buffer.length() < textInput.MaxLength) {
                                 // Safety clamp again just in case
                                 if (textInput.CursorPos > textInput.Buffer.length()) textInput.CursorPos = (int)textInput.Buffer.length();
                                 
                                 textInput.Buffer.insert(textInput.CursorPos, 1, c);
                                 textInput.CursorPos++;
                                 if (textInput.OnChange) textInput.OnChange(textInput.Buffer);
                             }
                         }
                    }
                }
                
                // Handle Enter (optional, e.g. lose focus)
                if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                    // textInput.IsFocused = false; // Optional
                }
            }
        }
    }

    void UIRenderer::UpdateAnimations(entt::registry& registry, float deltaTime) {
        auto view = registry.view<AnimationComponent>();
        view.each([deltaTime, &registry](auto entity, auto& animComp) {
            for (auto& anim : animComp.Animations) {
                if (anim.Finished) continue;

                anim.Elapsed += deltaTime;
                float t = anim.Elapsed / anim.Duration;
                
                if (t >= 1.0f) {
                    if (anim.Loop) {
                        anim.Elapsed = 0.0f;
                        t = 0.0f;
                    } else {
                        t = 1.0f;
                        anim.Finished = true;
                    }
                }

                float easedT = Easing::Apply(t, anim.Easing);

                // Interpolate
                AnimationValue currentVal;
                
                // Float
                if (std::holds_alternative<float>(anim.StartVal.data) && std::holds_alternative<float>(anim.EndVal.data)) {
                    float start = std::get<float>(anim.StartVal.data);
                    float end = std::get<float>(anim.EndVal.data);
                    currentVal.data = start + (end - start) * easedT;
                }
                // ImVec2
                else if (std::holds_alternative<ImVec2>(anim.StartVal.data) && std::holds_alternative<ImVec2>(anim.EndVal.data)) {
                    ImVec2 start = std::get<ImVec2>(anim.StartVal.data);
                    ImVec2 end = std::get<ImVec2>(anim.EndVal.data);
                    currentVal.data = ImVec2(start.x + (end.x - start.x) * easedT, start.y + (end.y - start.y) * easedT);
                }
                // Color (ImU32) - Interpolate RGBA
                else if (std::holds_alternative<ImU32>(anim.StartVal.data) && std::holds_alternative<ImU32>(anim.EndVal.data)) {
                    ImU32 start = std::get<ImU32>(anim.StartVal.data);
                    ImU32 end = std::get<ImU32>(anim.EndVal.data);
                    
                    int sR = (start >> 0) & 0xFF;
                    int sG = (start >> 8) & 0xFF;
                    int sB = (start >> 16) & 0xFF;
                    int sA = (start >> 24) & 0xFF;

                    int eR = (end >> 0) & 0xFF;
                    int eG = (end >> 8) & 0xFF;
                    int eB = (end >> 16) & 0xFF;
                    int eA = (end >> 24) & 0xFF;

                    int cR = sR + (int)((eR - sR) * easedT);
                    int cG = sG + (int)((eG - sG) * easedT);
                    int cB = sB + (int)((eB - sB) * easedT);
                    int cA = sA + (int)((eA - sA) * easedT);

                    currentVal.data = IM_COL32(cR, cG, cB, cA);
                }

                if (anim.Apply) {
                    anim.Apply(currentVal);
                }

                // Custom Generic Update
                if (anim.CustomUpdate) {
                    anim.CustomUpdate(easedT, registry, entity);
                }

                if (anim.Finished && anim.OnComplete) {
                    anim.OnComplete();
                }
            }
            
            // Remove finished animations
            animComp.Animations.erase(
                std::remove_if(animComp.Animations.begin(), animComp.Animations.end(), 
                    [](const Animation& a) { return a.Finished; }),
                animComp.Animations.end());
        });
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
            if (!style.CalculatedVisible) return;

            ImVec2 p_min = transform.Position;
            ImVec2 p_max = ImVec2(p_min.x + transform.Size.x, p_min.y + transform.Size.y);

            // Handle Rotation (Pre-Draw)
            // We need to capture the current vertex index to transform them later
            int vtx_idx_start = draw_list->_VtxCurrentIdx;

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

            // Handle Parent Clipping (Recursive Clip would be better, but immediate parent is enough for now)
            bool parentClipped = false;
            if (registry.all_of<ParentComponent>(entity)) {
                entt::entity parent = registry.get<ParentComponent>(entity).ParentEntity;
                if (registry.valid(parent) && registry.all_of<ClipComponent, TransformComponent>(parent)) {
                    if (registry.get<ClipComponent>(parent).ClipChildren) {
                        const auto& pTrans = registry.get<TransformComponent>(parent);
                        ImVec2 clipMin = pTrans.Position;
                        ImVec2 clipMax = ImVec2(clipMin.x + pTrans.Size.x, clipMin.y + pTrans.Size.y);
                        
                        // Adjust for Window Header if parent is Window
                        if (registry.all_of<ContainerComponent>(parent) && 
                            registry.get<ContainerComponent>(parent).Type == ContainerType::Window) {
                            clipMin.y += 30.0f;
                        }
                        
                        draw_list->PushClipRect(clipMin, clipMax, true);
                        parentClipped = true;
                    }
                }
            }

            // Handle Self Clipping (Push)
            bool pushedClip = false;
            if (registry.any_of<ClipComponent>(entity)) {
                const auto& clip = registry.get<ClipComponent>(entity);
                if (clip.ClipChildren) {
                    draw_list->PushClipRect(p_min, p_max, true);
                    pushedClip = true;
                }
            }

            // --- Render Glow (Multi-Pass Fake Soft Shadow) ---
            if (registry.any_of<GlowComponent>(entity)) {
                const auto& glow = registry.get<GlowComponent>(entity);
                if (glow.Enabled && glow.Intensity > 0.0f) {
                    // C4244 Fix: Explicit cast to float for bitwise results
                    float r = (float)((glow.Color >> 0) & 0xFF);
                    float g = (float)((glow.Color >> 8) & 0xFF);
                    float b = (float)((glow.Color >> 16) & 0xFF);
                    float a = (float)((glow.Color >> 24) & 0xFF);
                    
                    // Reduce base alpha based on samples to avoid over-saturation
                    float stepAlpha = (a * glow.Intensity) / (float)glow.Samples;
                    // Clamp
                    if (stepAlpha > 255.0f) stepAlpha = 255.0f;
                    
                    for (int i = 0; i < glow.Samples; i++) {
                        float dist = glow.Radius * ((float)(i + 1) / (float)glow.Samples);
                        ImU32 stepColor = IM_COL32((int)r, (int)g, (int)b, (int)(stepAlpha * (1.0f - (float)i/glow.Samples)));
                        
                        draw_list->AddRect(
                            ImVec2(p_min.x - dist, p_min.y - dist), 
                            ImVec2(p_max.x + dist, p_max.y + dist), 
                            stepColor, 
                            style.Rounding + dist, 
                            style.RoundingFlags, 
                            1.5f // Thickness
                        );
                    }
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

                // Handle Selection (Track best candidate)
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    ImVec2 mousePos = ImGui::GetMousePos();
                    if (mousePos.x >= p_min.x && mousePos.x <= p_max.x &&
                        mousePos.y >= p_min.y && mousePos.y <= p_max.y) {
                        
                        // Ignore if this entity is the Inspector window itself (heuristic: title "Inspector")
                        // But wait, the Inspector is an ImGui window, not our entity.
                        // However, if we have an entity named "Inspector", we might want to ignore it?
                        // Assuming standard ImGui windows don't interfere here unless they cover.
                        
                        // We set this every time we find a match.
                        // Since we iterate in Z-Order (Background -> Foreground), 
                        // the last one to write to s_HoveredDebugEntity will be the top-most one.
                        s_HoveredDebugEntity = entity;
                    }
                }

                // Highlight Selected
                if (s_SelectedEntity == entity) {
                    draw_list->AddRect(p_min, p_max, IM_COL32(255, 255, 0, 255), 0.0f, 0, 2.0f);
                } else if (!isPrecisionText) {
                    // Only show green box if this is the currently hovered candidate (for visual feedback)
                    // But we don't know if it's the FINAL candidate until end of loop.
                    // So just draw it if it's potentially selectable? 
                    // No, too much clutter. Let's remove the green box for everything 
                    // and only show it for the one under mouse?
                    // For now, keep original behavior or maybe tone it down.
                    // draw_list->AddRect(p_min, p_max, IM_COL32(0, 255, 0, 255), 0.0f, 0, 1.0f);
                } else {
                    // Precision Mode Debug: Draw bounds for each line of text
                const auto& text = registry.get<TextComponent>(entity);
                auto lines = CalculateTextLines(text, transform, &container);
                for (const auto& line : lines) {
                    // Apply scroll offset if present
                    float scrollOffset = 0.0f;
                    if (registry.any_of<ScrollComponent>(entity)) {
                        scrollOffset = registry.get<ScrollComponent>(entity).ScrollY;
                    }
                    
                    // Draw yellow bounding box for each text line
                    draw_list->AddRect(ImVec2(line.x, line.y - scrollOffset), ImVec2(line.x + line.z, line.y + line.w - scrollOffset), IM_COL32(255, 255, 0, 255));
                }
                // Also draw the container name if present
                    if (container.Name) {
                        draw_list->AddText(ImVec2(p_min.x, p_min.y - 15), IM_COL32(255, 255, 0, 255), container.Name);
                    }
                }
                
                if (container.Name && !isPrecisionText && s_SelectedEntity == entity) {
                    draw_list->AddText(ImVec2(p_min.x, p_min.y - 15), IM_COL32(0, 255, 0, 255), container.Name);
                }
            }



            // Render Text Content (Rich Text Support)
            if (registry.any_of<TextComponent>(entity)) {
                const auto& textComp = registry.get<TextComponent>(entity);
                bool hasContent = (!textComp.RawText.empty() || !textComp.Spans.empty());

                if (hasContent) {
                    ImVec2 text_start_pos = ImVec2(p_min.x + 10.0f, p_min.y + (container.Type == ContainerType::Window ? 40.0f : 10.0f));
                    float availableWidth = transform.Size.x - 20.0f;
                    
                    // Apply Scroll Offset if present on the same entity
                    if (registry.any_of<ScrollComponent>(entity)) {
                        text_start_pos.y -= registry.get<ScrollComponent>(entity).ScrollY;
                    }

                    if (textComp.Clip()) {
                        draw_list->PushClipRect(p_min, p_max, true);
                    }

                    int charIndexCounter = 0;

                    auto DrawTextSpan = [&](const std::string& str, const ImVec2& pos, ImU32 col, bool bold) {
                        if (textComp.CharacterTransformCallback) {
                             float currentX = pos.x;
                             ImFont* font = ImGui::GetFont();
                             float fontSize = ImGui::GetFontSize();

                             for (size_t i = 0; i < str.length(); ++i) {
                                 char c = str[i];
                                 char s[2] = { c, 0 };
                                 ImVec2 charSize = ImGui::CalcTextSize(s);
                                 
                                 // Initial State
                                 ImVec2 charPos = ImVec2(currentX, pos.y);
                                 float rotation = 0.0f;
                                 ImU32 charColor = col;
                                 float scale = 1.0f;

                                 // Callback
                                 textComp.CharacterTransformCallback(charIndexCounter, c, charPos, rotation, charColor, scale);
                                 
                                 if (rotation == 0.0f && scale == 1.0f) {
                                     draw_list->AddText(charPos, charColor, s);
                                 } else {
                                     // Custom Rotation/Scale
                                     int vtx_start = draw_list->_VtxCurrentIdx;
                                     draw_list->AddText(font, fontSize * scale, charPos, charColor, s);
                                     int vtx_end = draw_list->_VtxCurrentIdx;
                                     
                                     if (rotation != 0.0f) {
                                         // Rotate around CENTER of the character
                                         ImVec2 center = ImVec2(charPos.x + charSize.x * 0.5f * scale, charPos.y + charSize.y * 0.5f * scale);
                                         float rad = rotation * 3.14159f / 180.0f;
                                         float s_sin = sinf(rad);
                                         float c_cos = cosf(rad);
                                         
                                         for (int v = vtx_start; v < vtx_end; v++) {
                                             ImDrawVert& vert = draw_list->VtxBuffer[v];
                                             float px = vert.pos.x - center.x;
                                             float py = vert.pos.y - center.y;
                                             vert.pos.x = px * c_cos - py * s_sin + center.x;
                                             vert.pos.y = px * s_sin + py * c_cos + center.y;
                                         }
                                     }
                                 }
                                 
                                 currentX += charSize.x;
                                 charIndexCounter++;
                             }
                        } else {
                            if (bold) {
                                draw_list->AddText(ImVec2(pos.x + 1, pos.y), col, str.c_str());
                                draw_list->AddText(pos, col, str.c_str());
                            } else {
                                draw_list->AddText(pos, col, str.c_str());
                            }
                            charIndexCounter += (int)str.length();
                        }
                    };

                    ImVec2 cursor = text_start_pos;
                    float startX = text_start_pos.x;
                    
                    // Use shared layout logic
                    std::vector<RenderLine> lines = CalculateLayout(textComp, availableWidth);

                    // Update Scroll Content Height dynamically
                    if (registry.any_of<ScrollComponent>(entity)) {
                        float totalTextHeight = 0.0f;
                        for(const auto& line : lines) totalTextHeight += line.height;
                        // Add some padding
                        totalTextHeight += 20.0f; 
                        registry.get<ScrollComponent>(entity).ContentHeight = totalTextHeight;
                    }

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
                    // For custom components (like buttons/toggles), we often want the "Just Clicked" state
                    // to trigger actions once. IsClicked is true while held down.
                    // We can check ImGui::IsMouseClicked(0) && isHovered for a "Click Event"
                    // But the user lambda might expect isClicked to be true.
                    // Let's pass the raw state, but also ensure hit testing passed.
                    isClicked = input.IsClicked;
                }
                
                // CRITICAL FIX: Custom components often need to handle their own "Toggle" logic.
                // If we rely on the lambda's internal static variable, it works, BUT
                // we must ensure that the lambda is called correctly.
                // The issue user reported: "button doesn't turn green".
                // This implies the lambda logic `if (clicked && ImGui::IsMouseClicked...)` might be failing
                // because `clicked` (which comes from `input.IsClicked`) might be false if `UpdateInput` 
                // didn't flag it, OR `ImGui::IsMouseClicked` is consumed.
                // Actually, `input.IsClicked` is true if `mouseClicked` (frame 0) or `mouseDown` (subsequent).
                // In `UpdateInput`, we set `IsClicked = true` on `mouseClicked`.
                // So passing `isClicked` is correct.
                
                if (custom.OnRender) {
                    custom.OnRender(registry, entity, draw_list, p_min, p_max, isHovered, isClicked);
                }
            }

            // Slider
            if (registry.any_of<SliderComponent>(entity)) {
                auto& slider = registry.get<SliderComponent>(entity);
                
                // Logic
                if (slider.IsDragging) {
                    float mouseX = ImGui::GetMousePos().x;
                    float trackStart = p_min.x;
                    float trackWidth = p_max.x - p_min.x;
                    
                    // Interaction Sensitivity (simple approach: just standard mapping for now, but could be scaled)
                    // "Scroll per area" requested - usually means scaling the delta, but absolute positioning is standard for sliders.
                    // If sensitivity != 1.0, we might need relative drag mode. 
                    // For now, let's keep absolute mapping as it's most intuitive for standard sliders.
                    // If user wants "sensitivity", it usually applies to infinite sliders or knobs.
                    // We'll stick to 1:1 mapping for the bar, but maybe smooth the result.

                    float normalized = (mouseX - trackStart) / trackWidth;
                    if (normalized < 0.0f) normalized = 0.0f;
                    if (normalized > 1.0f) normalized = 1.0f;
                    
                    float newValue = slider.Min + normalized * (slider.Max - slider.Min);
                    
                    // Snap to Int
                    if (slider.DataType == SliderDataType::Int) {
                        newValue = std::round(newValue);
                    }

                    if (newValue != slider.Value) {
                        slider.Value = newValue;
                        if (!slider.EnableSmoothing) slider.VisualValue = newValue; // Snap visual if no smoothing
                        if (slider.OnChange) slider.OnChange(slider.Value);
                    }
                }

                // Update Visual Value (Smoothing)
                if (slider.EnableSmoothing) {
                    float diff = slider.Value - slider.VisualValue;
                    if (fabs(diff) > 0.001f) {
                        // Lerp
                        float dt = ImGui::GetIO().DeltaTime;
                        slider.VisualValue += diff * slider.SmoothingSpeed * dt;
                    } else {
                        slider.VisualValue = slider.Value;
                    }
                } else {
                    slider.VisualValue = slider.Value;
                }

                // Render
                float trackH = slider.TrackHeight;
                float trackY = p_min.y + (p_max.y - p_min.y) * 0.5f - trackH * 0.5f;
                
                // Track Background
                draw_list->AddRectFilled(ImVec2(p_min.x, trackY), ImVec2(p_max.x, trackY + trackH), slider.ColorTrack, 2.0f);
                
                // Fill
                float fillRatio = (slider.VisualValue - slider.Min) / (slider.Max - slider.Min);
                if (fillRatio < 0.0f) fillRatio = 0.0f;
                if (fillRatio > 1.0f) fillRatio = 1.0f;
                
                float fillWidth = fillRatio * (p_max.x - p_min.x);
                draw_list->AddRectFilled(ImVec2(p_min.x, trackY), ImVec2(p_min.x + fillWidth, trackY + trackH), slider.ColorFill, 2.0f);
                
                // Knob
                float knobX = p_min.x + fillWidth;
                draw_list->AddCircleFilled(ImVec2(knobX, trackY + trackH * 0.5f), slider.KnobRadius, slider.ColorKnob);
                
                // Hover Effect (Outer Glow)
                if (registry.any_of<InputStateComponent>(entity) && registry.get<InputStateComponent>(entity).IsHovered) {
                     draw_list->AddCircle(ImVec2(knobX, trackY + trackH * 0.5f), slider.KnobRadius + 1.0f, IM_COL32(200, 200, 255, 100), 12, 2.0f);
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
                    // Correctly position cursor based on CursorPos index
                    // (std::min) prevents macro expansion on Windows
                    std::string sub = textInput.Buffer.substr(0, (std::min)((size_t)textInput.CursorPos, textInput.Buffer.length()));
                    ImVec2 subSize = ImGui::CalcTextSize(sub.c_str());
                    float cursorX = textPos.x + subSize.x;
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

            if (parentClipped) {
                draw_list->PopClipRect();
            }

            // Handle Rotation (Post-Draw Vertex Transformation)
            if (transform.Rotation != 0.0f) {
                int vtx_idx_end = draw_list->_VtxCurrentIdx;
                
                // Calculate Center
                ImVec2 center = ImVec2(p_min.x + transform.Size.x * 0.5f, p_min.y + transform.Size.y * 0.5f);
                float s = sinf(transform.Rotation * 3.14159f / 180.0f);
                float c = cosf(transform.Rotation * 3.14159f / 180.0f);

                for (int i = vtx_idx_start; i < vtx_idx_end; i++) {
                    ImDrawVert& v = draw_list->VtxBuffer[i];
                    // Translate to origin
                    float px = v.pos.x - center.x;
                    float py = v.pos.y - center.y;
                    
                    // Rotate
                    float nx = px * c - py * s;
                    float ny = px * s + py * c;
                    
                    // Translate back
                    v.pos.x = nx + center.x;
                    v.pos.y = ny + center.y;
                }
            }
        });

        // Commit Selection (if any clicked)
        if (DebugMode && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
             // Only update selection if we actually hovered something in our UI layer.
             // If we clicked an ImGui window (Inspector), s_HoveredDebugEntity would have been updated 
             // IF the ImGui window was transparent to clicks, but ImGui consumes clicks.
             // We need to check if mouse is NOT hovering an ImGui window.
             if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
                 s_SelectedEntity = s_HoveredDebugEntity;
             }
             // Reset for next frame
             s_HoveredDebugEntity = entt::null;
        }
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

    entt::entity UIRenderer::FindChildByName(entt::registry& registry, entt::entity parent, const char* name) {
        auto view = registry.view<ParentComponent, ContainerComponent>();
        for (auto entity : view) {
            const auto& pc = view.get<ParentComponent>(entity);
            if (pc.ParentEntity == parent) {
                const auto& cc = view.get<ContainerComponent>(entity);
                if (cc.Name && strcmp(cc.Name, name) == 0) {
                    return entity;
                }
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
