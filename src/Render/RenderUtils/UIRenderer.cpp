#include "UIRenderer.hpp"
#include "Dx12Init/Dx12Init.hpp"
#include "Components/ImageLoaderComponent.hpp" // Added for ImageLoader
#include "ShaderSystem.hpp"
#include <unordered_map>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <array>
#include <vector>


// UIComponents.hpp is already included in UIRenderer.hpp, which now includes the sub-files.

namespace RenderUtils {

    bool UIRenderer::DebugMode = false;


    // --- Internal Layout Helper ---
    namespace {
        constexpr float kDegToRad = 3.1415926535f / 180.0f;

        void RotateVertices(ImDrawList* draw_list, int vtx_idx_start, int vtx_idx_end, const ImVec2& center, float rotationDegrees) {
            if (!draw_list || vtx_idx_end <= vtx_idx_start || rotationDegrees == 0.0f) {
                return;
            }

            float s = sinf(rotationDegrees * kDegToRad);
            float c = cosf(rotationDegrees * kDegToRad);

            const int maxIdx = (int)draw_list->VtxBuffer.Size;
            const int start = (std::max)(0, vtx_idx_start);
            const int end = (std::min)(maxIdx, vtx_idx_end);

            for (int i = start; i < end; ++i) {
                ImDrawVert& v = draw_list->VtxBuffer[i];
                float px = v.pos.x - center.x;
                float py = v.pos.y - center.y;
                v.pos.x = px * c - py * s + center.x;
                v.pos.y = px * s + py * c + center.y;
            }
        }

        inline bool IsEntityVisible(entt::registry& registry, entt::entity entity) {
            const auto* style = registry.try_get<StyleComponent>(entity);
            return !style || style->CalculatedVisible;
        }

        inline float Clamp01(float value) {
            if (!std::isfinite(value)) {
                return 0.0f;
            }
            if (value < 0.0f) {
                return 0.0f;
            }
            if (value > 1.0f) {
                return 1.0f;
            }
            return value;
        }

        inline ImU32 ScaleColorAlpha(ImU32 color, float alphaMultiplier) {
            const float clampedMultiplier = Clamp01(alphaMultiplier);
            const int r = (color >> 0) & 0xFF;
            const int g = (color >> 8) & 0xFF;
            const int b = (color >> 16) & 0xFF;
            const int a = (color >> 24) & 0xFF;
            const int scaledAlpha = static_cast<int>(static_cast<float>(a) * clampedMultiplier);
            return IM_COL32(r, g, b, scaledAlpha);
        }

        inline std::vector<entt::entity> SortCustomEntities(entt::registry& registry, bool requireInput) {
            std::vector<entt::entity> entities;
            if (requireInput) {
                auto view = registry.view<CustomComponent, InputStateComponent>();
                entities.assign(view.begin(), view.end());
            } else {
                auto view = registry.view<CustomComponent>();
                entities.assign(view.begin(), view.end());
            }

            std::sort(entities.begin(), entities.end(), [&registry](entt::entity lhs, entt::entity rhs) {
                const auto& lhsCustom = registry.get<CustomComponent>(lhs);
                const auto& rhsCustom = registry.get<CustomComponent>(rhs);
                if (lhsCustom.Priority != rhsCustom.Priority) {
                    return lhsCustom.Priority > rhsCustom.Priority;
                }

                const int lhsZ = registry.all_of<StyleComponent>(lhs) ? registry.get<StyleComponent>(lhs).ZIndexInt : 0;
                const int rhsZ = registry.all_of<StyleComponent>(rhs) ? registry.get<StyleComponent>(rhs).ZIndexInt : 0;
                if (lhsZ != rhsZ) {
                    return lhsZ > rhsZ;
                }

                return entt::to_integral(lhs) < entt::to_integral(rhs);
            });

            return entities;
        }

        inline void InvokeCustomUpdateCallbacks(entt::registry& registry, float deltaTime) {
            const auto entities = SortCustomEntities(registry, false);
            for (auto entity : entities) {
                if (!registry.valid(entity) || !registry.any_of<CustomComponent>(entity)) {
                    continue;
                }
                auto& custom = registry.get<CustomComponent>(entity);
                if (!custom.Enabled || !custom.OnUpdate || !IsEntityVisible(registry, entity)) {
                    continue;
                }
                custom.OnUpdate(registry, entity, deltaTime);
            }
        }

        inline void InvokeCustomInputCallbacks(entt::registry& registry) {
            const auto entities = SortCustomEntities(registry, true);
            for (auto entity : entities) {
                if (!registry.valid(entity) || !registry.all_of<CustomComponent, InputStateComponent>(entity)) {
                    continue;
                }
                auto& custom = registry.get<CustomComponent>(entity);
                if (!custom.Enabled || !custom.OnInput || !IsEntityVisible(registry, entity)) {
                    continue;
                }
                custom.OnInput(registry, entity, registry.get<InputStateComponent>(entity));
            }
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
                    ImGui::DragFloat2("Content Padding", &sc.ContentPaddingX, 0.25f, 0.0f, 100.0f);
                    ImGui::Checkbox("Use Gradient", &sc.UseGradient);
                    if (sc.UseGradient) {
                        ImVec4 gTop = ImColor(sc.GradientTopColor);
                        ImVec4 gBottom = ImColor(sc.GradientBottomColor);
                        if (ImGui::ColorEdit4("Gradient Top", (float*)&gTop)) {
                            sc.GradientTopColor = ImColor(gTop);
                        }
                        if (ImGui::ColorEdit4("Gradient Bottom", (float*)&gBottom)) {
                            sc.GradientBottomColor = ImColor(gBottom);
                        }
                    }
                    ImGui::Checkbox("Outline Enabled", &sc.OutlineEnabled);
                    if (sc.OutlineEnabled) {
                        ImVec4 outCol = ImColor(sc.OutlineColor);
                        if (ImGui::ColorEdit4("Outline Color", (float*)&outCol)) {
                            sc.OutlineColor = ImColor(outCol);
                        }
                        ImGui::DragFloat("Outline Thickness", &sc.OutlineThickness, 0.1f, 0.0f, 20.0f);
                    }
                }
            }

            // 4. Text Component
            if (registry.all_of<TextComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& txt = registry.get<TextComponent>(s_SelectedEntity);
                    // Use a static buffer for editing text (simple version)
                    static char buf[256];
                    std::strncpy(buf, txt.RawText.c_str(), sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
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
                    int renderMode = static_cast<int>(glow.RenderMode);
                    const char* renderModes[] = { "CPU", "Shader" };
                    if (ImGui::Combo("Render Mode##Glow", &renderMode, renderModes, 2)) {
                        glow.RenderMode = static_cast<GlowRenderMode>(renderMode);
                    }
                    std::array<char, 128> glowShaderKey{};
                    std::strncpy(glowShaderKey.data(), glow.ShaderKey.c_str(), glowShaderKey.size() - 1);
                    if (ImGui::InputText("Shader Key##Glow", glowShaderKey.data(), glowShaderKey.size())) {
                        glow.ShaderKey = glowShaderKey.data();
                    }
                    if (ImGui::DragFloat("Radius", &glow.Radius, 1.0f, 0.0f, 100.0f)) {
                        glow.MarkCacheDirty();
                    }
                    if (ImGui::DragFloat("Intensity", &glow.Intensity, 0.01f, 0.0f, 8.0f)) {
                        glow.MarkCacheDirty();
                    }
                    if (ImGui::DragInt("Samples", &glow.Samples, 1, 1, 64)) {
                        glow.MarkCacheDirty();
                    }
                    if (ImGui::Checkbox("Cache Enabled", &glow.CacheEnabled)) {
                        glow.MarkCacheDirty();
                    }
                    if (ImGui::DragInt("Max Samples", &glow.MaxSamples, 1, 1, 96)) {
                        glow.MarkCacheDirty();
                    }

                    int mode = static_cast<int>(glow.Mode);
                    const char* modes[] = { "Gaussian Bloom", "Neon Tube", "Ambient Soft" };
                    if (ImGui::Combo("Mode", &mode, modes, 3)) {
                        glow.Mode = static_cast<GlowMode>(mode);
                        glow.MarkCacheDirty();
                    }
                    int quality = static_cast<int>(glow.QualityMode);
                    const char* qualityModes[] = { "Performance", "Balanced", "Ultra" };
                    if (ImGui::Combo("Quality", &quality, qualityModes, 3)) {
                        glow.QualityMode = static_cast<GlowQualityMode>(quality);
                        glow.MarkCacheDirty();
                    }
                    if (ImGui::DragFloat("Falloff", &glow.Falloff, 0.01f, 0.2f, 4.0f)) {
                        glow.MarkCacheDirty();
                    }
                    if (ImGui::DragFloat("Core Strength", &glow.CoreStrength, 0.01f, 0.0f, 2.0f)) {
                        glow.MarkCacheDirty();
                    }
                    if (ImGui::Checkbox("Inner Glow", &glow.InnerGlow)) {
                        glow.MarkCacheDirty();
                    }
                    if (ImGui::Checkbox("Outer Only", &glow.OuterOnly)) {
                        glow.MarkCacheDirty();
                    }
                    if (ImGui::DragFloat("Radius Scale", &glow.RadiusScale, 0.01f, 0.1f, 3.0f)) {
                        glow.MarkCacheDirty();
                    }
                    
                    ImVec4 gCol = ImColor(glow.Color);
                    if (ImGui::ColorEdit4("Glow Color", (float*)&gCol)) {
                        glow.Color = ImColor(gCol);
                        glow.MarkCacheDirty();
                    }
                }
            }

            if (registry.all_of<ShadowComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& shadow = registry.get<ShadowComponent>(s_SelectedEntity);
                    ImGui::Checkbox("Enabled##shadow", &shadow.Enabled);
                    int renderMode = static_cast<int>(shadow.RenderMode);
                    const char* renderModes[] = { "CPU", "Shader" };
                    if (ImGui::Combo("Render Mode##Shadow", &renderMode, renderModes, 2)) {
                        shadow.RenderMode = static_cast<ShadowRenderMode>(renderMode);
                    }
                    std::array<char, 128> shadowShaderKey{};
                    std::strncpy(shadowShaderKey.data(), shadow.ShaderKey.c_str(), shadowShaderKey.size() - 1);
                    if (ImGui::InputText("Shader Key##Shadow", shadowShaderKey.data(), shadowShaderKey.size())) {
                        shadow.ShaderKey = shadowShaderKey.data();
                    }
                    ImVec4 shadowCol = ImColor(shadow.Color);
                    if (ImGui::ColorEdit4("Shadow Color", (float*)&shadowCol)) {
                        shadow.Color = ImColor(shadowCol);
                    }
                    ImGui::DragFloat2("Shadow Offset", &shadow.Offset.x, 0.25f);
                    ImGui::DragFloat("Blur Radius", &shadow.BlurRadius, 0.5f, 0.0f, 100.0f);
                    ImGui::DragFloat("Spread", &shadow.Spread, 0.5f, -50.0f, 50.0f);
                    ImGui::DragInt("Shadow Samples", &shadow.Samples, 1, 1, 48);
                    ImGui::Checkbox("Inset", &shadow.Inset);
                }
            }

            if (registry.all_of<ShaderComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Shader", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& shader = registry.get<ShaderComponent>(s_SelectedEntity);
                    ImGui::Checkbox("Enabled##shader", &shader.Enabled);

                    int backend = static_cast<int>(shader.Backend);
                    const char* backendModes[] = { "AutoPreferDXC", "D3DCompileOnly", "DXCOnly" };
                    if (ImGui::Combo("Backend", &backend, backendModes, 3)) {
                        shader.Backend = static_cast<ShaderBackendMode>(backend);
                        shader.Dirty = true;
                    }

                    int policy = static_cast<int>(shader.CompilePolicy);
                    const char* compilePolicies[] = { "OnDemand+Cache", "Startup", "Manual Apply" };
                    if (ImGui::Combo("Compile Policy", &policy, compilePolicies, 3)) {
                        shader.CompilePolicy = static_cast<ShaderCompilePolicy>(policy);
                    }

                    int stage = static_cast<int>(shader.StageMode);
                    const char* stageModes[] = { "PixelOnly", "VertexAndPixel" };
                    if (ImGui::Combo("Stage Mode", &stage, stageModes, 2)) {
                        shader.StageMode = static_cast<ShaderStageMode>(stage);
                        shader.Dirty = true;
                    }

                    int sourceMode = static_cast<int>(shader.PixelSource.Mode);
                    const char* sourceModes[] = { "EmbeddedCpp", "EmbeddedRust", "File", "Inline" };
                    if (ImGui::Combo("Pixel Source Mode", &sourceMode, sourceModes, 4)) {
                        shader.PixelSource.Mode = static_cast<ShaderSourceMode>(sourceMode);
                        shader.Dirty = true;
                    }
                    ImGui::TextDisabled("File-only policy: Embedded* modes resolve alias keys to files.");
                    if (shader.PixelSource.Mode == ShaderSourceMode::Inline) {
                        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.38f, 1.0f), "Inline source is disabled by policy.");
                    }

                    std::array<char, 512> pixelSourceBuf{};
                    std::strncpy(pixelSourceBuf.data(), shader.PixelSource.KeyOrPathOrInline.c_str(), pixelSourceBuf.size() - 1);
                    if (ImGui::InputTextMultiline("Pixel Source/Key", pixelSourceBuf.data(), pixelSourceBuf.size(), ImVec2(430.0f, 82.0f))) {
                        shader.PixelSource.KeyOrPathOrInline = pixelSourceBuf.data();
                        shader.Dirty = true;
                    }

                    std::array<char, 64> pixelEntryBuf{};
                    std::strncpy(pixelEntryBuf.data(), shader.PixelSource.EntryPoint.c_str(), pixelEntryBuf.size() - 1);
                    if (ImGui::InputText("Pixel Entry", pixelEntryBuf.data(), pixelEntryBuf.size())) {
                        shader.PixelSource.EntryPoint = pixelEntryBuf.data();
                        shader.Dirty = true;
                    }

                    std::array<char, 64> pixelTargetBuf{};
                    std::strncpy(pixelTargetBuf.data(), shader.PixelSource.TargetProfile.c_str(), pixelTargetBuf.size() - 1);
                    if (ImGui::InputText("Pixel Target", pixelTargetBuf.data(), pixelTargetBuf.size())) {
                        shader.PixelSource.TargetProfile = pixelTargetBuf.data();
                        shader.Dirty = true;
                    }

                    if (shader.StageMode == ShaderStageMode::VertexAndPixel) {
                        int vsMode = static_cast<int>(shader.VertexSource.Mode);
                        if (ImGui::Combo("Vertex Source Mode", &vsMode, sourceModes, 4)) {
                            shader.VertexSource.Mode = static_cast<ShaderSourceMode>(vsMode);
                            shader.Dirty = true;
                        }
                        if (shader.VertexSource.Mode == ShaderSourceMode::Inline) {
                            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.38f, 1.0f), "Inline source is disabled by policy.");
                        }

                        std::array<char, 512> vertexSourceBuf{};
                        std::strncpy(vertexSourceBuf.data(), shader.VertexSource.KeyOrPathOrInline.c_str(), vertexSourceBuf.size() - 1);
                        if (ImGui::InputTextMultiline("Vertex Source/Key", vertexSourceBuf.data(), vertexSourceBuf.size(), ImVec2(430.0f, 62.0f))) {
                            shader.VertexSource.KeyOrPathOrInline = vertexSourceBuf.data();
                            shader.Dirty = true;
                        }

                        std::array<char, 64> vertexEntryBuf{};
                        std::strncpy(vertexEntryBuf.data(), shader.VertexSource.EntryPoint.c_str(), vertexEntryBuf.size() - 1);
                        if (ImGui::InputText("Vertex Entry", vertexEntryBuf.data(), vertexEntryBuf.size())) {
                            shader.VertexSource.EntryPoint = vertexEntryBuf.data();
                            shader.Dirty = true;
                        }

                        std::array<char, 64> vertexTargetBuf{};
                        std::strncpy(vertexTargetBuf.data(), shader.VertexSource.TargetProfile.c_str(), vertexTargetBuf.size() - 1);
                        if (ImGui::InputText("Vertex Target", vertexTargetBuf.data(), vertexTargetBuf.size())) {
                            shader.VertexSource.TargetProfile = vertexTargetBuf.data();
                            shader.Dirty = true;
                        }
                    }

                    ImGui::SeparatorText("Parameters");
                    int removeParamIndex = -1;
                    for (size_t i = 0; i < shader.Parameters.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        auto& param = shader.Parameters[i];

                        std::array<char, 96> paramName{};
                        std::strncpy(paramName.data(), param.Name.c_str(), paramName.size() - 1);
                        if (ImGui::InputText("Name", paramName.data(), paramName.size())) {
                            param.Name = paramName.data();
                            shader.Dirty = true;
                        }

                        int paramType = static_cast<int>(param.Type);
                        const char* paramTypes[] = { "Float", "Int", "Vec2", "Vec3", "Vec4", "Color", "Bool" };
                        if (ImGui::Combo("Type", &paramType, paramTypes, 7)) {
                            param.Type = static_cast<ShaderParamType>(paramType);
                            switch (param.Type) {
                            case ShaderParamType::Float: param.Value = 0.0f; break;
                            case ShaderParamType::Int: param.Value = 0; break;
                            case ShaderParamType::Vec2: param.Value = ImVec2(0.0f, 0.0f); break;
                            case ShaderParamType::Vec3: param.Value = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); break;
                            case ShaderParamType::Vec4: param.Value = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); break;
                            case ShaderParamType::Color: param.Value = IM_COL32(255, 255, 255, 255); break;
                            case ShaderParamType::Bool: param.Value = false; break;
                            }
                            shader.Dirty = true;
                        }

                        switch (param.Type) {
                        case ShaderParamType::Float: {
                            float value = std::holds_alternative<float>(param.Value) ? std::get<float>(param.Value) : 0.0f;
                            if (ImGui::DragFloat("Value", &value, 0.01f)) {
                                param.Value = value;
                                shader.Dirty = true;
                            }
                            break;
                        }
                        case ShaderParamType::Int: {
                            int value = std::holds_alternative<int>(param.Value) ? std::get<int>(param.Value) : 0;
                            if (ImGui::DragInt("Value", &value, 1.0f)) {
                                param.Value = value;
                                shader.Dirty = true;
                            }
                            break;
                        }
                        case ShaderParamType::Vec2: {
                            ImVec2 value = std::holds_alternative<ImVec2>(param.Value) ? std::get<ImVec2>(param.Value) : ImVec2(0.0f, 0.0f);
                            if (ImGui::DragFloat2("Value", &value.x, 0.01f)) {
                                param.Value = value;
                                shader.Dirty = true;
                            }
                            break;
                        }
                        case ShaderParamType::Vec3: {
                            ImVec4 value = std::holds_alternative<ImVec4>(param.Value) ? std::get<ImVec4>(param.Value) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
                            float v3[3] = { value.x, value.y, value.z };
                            if (ImGui::DragFloat3("Value", v3, 0.01f)) {
                                param.Value = ImVec4(v3[0], v3[1], v3[2], 0.0f);
                                shader.Dirty = true;
                            }
                            break;
                        }
                        case ShaderParamType::Vec4: {
                            ImVec4 value = std::holds_alternative<ImVec4>(param.Value) ? std::get<ImVec4>(param.Value) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
                            if (ImGui::DragFloat4("Value", &value.x, 0.01f)) {
                                param.Value = value;
                                shader.Dirty = true;
                            }
                            break;
                        }
                        case ShaderParamType::Color: {
                            ImU32 value = std::holds_alternative<ImU32>(param.Value) ? std::get<ImU32>(param.Value) : IM_COL32(255, 255, 255, 255);
                            ImVec4 color = ImColor(value);
                            if (ImGui::ColorEdit4("Value", &color.x)) {
                                param.Value = static_cast<ImU32>(ImColor(color));
                                shader.Dirty = true;
                            }
                            break;
                        }
                        case ShaderParamType::Bool: {
                            bool value = std::holds_alternative<bool>(param.Value) ? std::get<bool>(param.Value) : false;
                            if (ImGui::Checkbox("Value", &value)) {
                                param.Value = value;
                                shader.Dirty = true;
                            }
                            break;
                        }
                        }

                        ImGui::Checkbox("Expose In Inspector", &param.ExposedInInspector);
                        if (ImGui::SmallButton("Remove Param")) {
                            removeParamIndex = static_cast<int>(i);
                        }
                        ImGui::Separator();
                        ImGui::PopID();
                    }
                    if (removeParamIndex >= 0 &&
                        removeParamIndex < static_cast<int>(shader.Parameters.size())) {
                        shader.Parameters.erase(shader.Parameters.begin() + removeParamIndex);
                        shader.Dirty = true;
                    }
                    if (ImGui::Button("Add Parameter")) {
                        ShaderParameter param;
                        param.Name = "Param" + std::to_string(shader.Parameters.size());
                        param.Type = ShaderParamType::Float;
                        param.Value = 0.0f;
                        shader.Parameters.push_back(std::move(param));
                        shader.Dirty = true;
                    }

                    ImGui::Checkbox("Auto Reload Files", &shader.AutoReloadFileChanges);

                    if (ImGui::Button("Apply/Recompile")) {
                        shader.CompileRequested = true;
                        shader.Dirty = true;
                    }

                    ImGui::Text("Compiled: %s", shader.IsCompiled ? "Yes" : "No");
                    ImGui::Text("Program Handle: %llu", static_cast<unsigned long long>(shader.ProgramHandle));
                    if (!shader.LastError.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Last Error:");
                        ImGui::TextWrapped("%s", shader.LastError.c_str());
                    }
                }
            }

            if (registry.all_of<WindowHeaderComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Window Header", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& header = registry.get<WindowHeaderComponent>(s_SelectedEntity);
                    ImGui::Checkbox("Enabled##header", &header.Enabled);
                    ImGui::DragFloat("Height", &header.Height, 0.25f, 0.0f, 120.0f);
                    ImGui::DragFloat2("Padding", &header.PaddingX, 0.25f);
                    ImGui::Checkbox("Show Title", &header.ShowTitle);
                    ImGui::Checkbox("Drag From Header Only", &header.DragFromHeaderOnly);
                    ImGui::Checkbox("Clip Children Below Header", &header.ClipChildrenBelowHeader);

                    ImVec4 hBg = ImColor(header.BackgroundColor);
                    ImVec4 hText = ImColor(header.TextColor);
                    ImVec4 hLine = ImColor(header.SeparatorColor);
                    if (ImGui::ColorEdit4("Header BG", (float*)&hBg)) {
                        header.BackgroundColor = ImColor(hBg);
                    }
                    if (ImGui::ColorEdit4("Header Text", (float*)&hText)) {
                        header.TextColor = ImColor(hText);
                    }
                    if (ImGui::ColorEdit4("Header Separator", (float*)&hLine)) {
                        header.SeparatorColor = ImColor(hLine);
                    }
                }
            }

            if (registry.all_of<ShapeComponent>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& shape = registry.get<ShapeComponent>(s_SelectedEntity);
                    ImGui::Checkbox("Enabled##shape", &shape.Enabled);
                    ImGui::Checkbox("Draw Behind Content", &shape.DrawBehindContent);
                    ImGui::Checkbox("Clip To Entity", &shape.ClipToEntity);
                    ImGui::Checkbox("Use For HitTest", &shape.UseForHitTest);
                    ImGui::DragInt("Priority##shape", &shape.Priority, 1, -100, 100);
                    ImGui::Text("Shape Count: %d", static_cast<int>(shape.Shapes.size()));

                    if (ImGui::Button("Add Rect")) {
                        shape.Shapes.emplace_back(ShapeType::Rect);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Circle")) {
                        shape.Shapes.emplace_back(ShapeType::Circle);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Line")) {
                        ShapePrimitive primitive(ShapeType::Line);
                        primitive.Points = { ImVec2(0.0f, 0.0f), ImVec2(80.0f, 0.0f) };
                        primitive.StrokeEnabled = true;
                        shape.Shapes.push_back(std::move(primitive));
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Triangle")) {
                        ShapePrimitive primitive(ShapeType::Triangle);
                        primitive.Points = { ImVec2(0.0f, 80.0f), ImVec2(40.0f, 0.0f), ImVec2(80.0f, 80.0f) };
                        primitive.StrokeEnabled = true;
                        shape.Shapes.push_back(std::move(primitive));
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Polyline")) {
                        ShapePrimitive primitive(ShapeType::Polyline);
                        primitive.Points = { ImVec2(0.0f, 0.0f), ImVec2(40.0f, 10.0f), ImVec2(90.0f, -5.0f) };
                        primitive.StrokeEnabled = true;
                        shape.Shapes.push_back(std::move(primitive));
                    }

                    int removeShapeIndex = -1;
                    int duplicateShapeIndex = -1;
                    for (size_t i = 0; i < shape.Shapes.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        auto& primitive = shape.Shapes[i];
                        if (ImGui::TreeNode("Primitive", "Primitive %d", static_cast<int>(i))) {
                            int type = static_cast<int>(primitive.Type);
                            const char* typeNames[] = { "Rect", "Circle", "Line", "Triangle", "Polyline" };
                            if (ImGui::Combo("Type", &type, typeNames, 5)) {
                                primitive.Type = static_cast<ShapeType>(type);
                                if ((primitive.Type == ShapeType::Line || primitive.Type == ShapeType::Triangle || primitive.Type == ShapeType::Polyline) &&
                                    primitive.Points.empty()) {
                                    primitive.Points = { ImVec2(0.0f, 0.0f), ImVec2(80.0f, 0.0f) };
                                }
                            }

                            ImGui::Checkbox("Visible", &primitive.Visible);
                            ImGui::DragFloat2("Offset", &primitive.Offset.x, 0.2f);
                            ImGui::DragFloat2("Size", &primitive.Size.x, 0.2f);
                            ImGui::DragFloat("Radius", &primitive.Radius, 0.2f, 0.0f, 2048.0f);
                            ImGui::DragFloat("Rounding", &primitive.Rounding, 0.1f, 0.0f, 100.0f);
                            ImGui::Checkbox("Filled", &primitive.Filled);
                            ImGui::Checkbox("Stroke Enabled", &primitive.StrokeEnabled);
                            ImGui::DragFloat("Stroke Thickness", &primitive.StrokeThickness, 0.1f, 0.1f, 64.0f);

                            ImVec4 fill = ImColor(primitive.FillColor);
                            ImVec4 stroke = ImColor(primitive.StrokeColor);
                            if (ImGui::ColorEdit4("Fill Color", &fill.x)) {
                                primitive.FillColor = ImColor(fill);
                            }
                            if (ImGui::ColorEdit4("Stroke Color", &stroke.x)) {
                                primitive.StrokeColor = ImColor(stroke);
                            }

                            if (primitive.Type == ShapeType::Line || primitive.Type == ShapeType::Triangle || primitive.Type == ShapeType::Polyline) {
                                ImGui::SeparatorText("Points");
                                int removePointIndex = -1;
                                for (size_t p = 0; p < primitive.Points.size(); ++p) {
                                    ImGui::PushID(static_cast<int>(p));
                                    ImGui::DragFloat2("Point", &primitive.Points[p].x, 0.2f);
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("X")) {
                                        removePointIndex = static_cast<int>(p);
                                    }
                                    ImGui::PopID();
                                }
                                if (removePointIndex >= 0 && removePointIndex < static_cast<int>(primitive.Points.size())) {
                                    primitive.Points.erase(primitive.Points.begin() + removePointIndex);
                                }
                                if (ImGui::SmallButton("Add Point")) {
                                    primitive.Points.emplace_back(0.0f, 0.0f);
                                }
                            }

                            if (ImGui::SmallButton("Duplicate")) {
                                duplicateShapeIndex = static_cast<int>(i);
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Remove")) {
                                removeShapeIndex = static_cast<int>(i);
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }

                    if (duplicateShapeIndex >= 0 && duplicateShapeIndex < static_cast<int>(shape.Shapes.size())) {
                        shape.Shapes.push_back(shape.Shapes[static_cast<size_t>(duplicateShapeIndex)]);
                    }
                    if (removeShapeIndex >= 0 && removeShapeIndex < static_cast<int>(shape.Shapes.size())) {
                        shape.Shapes.erase(shape.Shapes.begin() + removeShapeIndex);
                    }
                }
            }

            if (registry.all_of<Components::ImageLoader>(s_SelectedEntity)) {
                if (ImGui::CollapsingHeader("Image Loader", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& loader = registry.get<Components::ImageLoader>(s_SelectedEntity);
                    ImGui::Text("State: %s",
                        loader.state == Components::ImageLoader::LoadState::Idle ? "Idle" :
                        loader.state == Components::ImageLoader::LoadState::Loading ? "Loading" :
                        loader.state == Components::ImageLoader::LoadState::Loaded ? "Loaded" : "Failed");
                    ImGui::Text("Source: %d / %d", loader.ActiveSourceIndex + 1, static_cast<int>(loader.source_states.size()));
                    int cycleMode = static_cast<int>(loader.CycleMode);
                    const char* cycleModes[] = { "Loaded Only", "Cycle All", "Refetch On Click" };
                    if (ImGui::Combo("Cycle Mode", &cycleMode, cycleModes, 3)) {
                        loader.CycleMode = static_cast<Components::ImageLoader::SourceCycleMode>(cycleMode);
                    }
                    ImGui::Checkbox("Skip Failed Sources", &loader.SkipFailedSources);
                    ImGui::Checkbox("Keep Last Successful", &loader.KeepLastSuccessfulTexture);
                    if (!loader.LastStatusMessage.empty()) {
                        ImGui::TextWrapped("Status: %s", loader.LastStatusMessage.c_str());
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
                        const float denom = (a.Duration <= 0.0f) ? 1.0f : a.Duration;
                        float t = a.Elapsed / denom;
                        if (t < 0.0f) t = 0.0f;
                        if (t > 1.0f) t = 1.0f;
                        ImGui::ProgressBar(t, ImVec2(-1, 0), "Progress");
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
                                     da.TargetEntityName = name; // Name lifetime is owned by ContainerComponent.
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
        Components::ImageLoaderSystem::Register(registry);
    }

    void UIRenderer::FreeImageResource(entt::registry& registry, entt::entity entity) {
        Components::ImageLoaderSystem::FreeEntity(registry, entity);
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

        // 3. Behavior-critical update order:
        // UpdateAnimations -> CustomUpdate -> ResolveTransforms -> ResolveDepth -> UpdateInput -> OptionsUpdate -> ShaderUpdate -> CustomInput -> UpdateText -> UpdateImageLoader
        UpdateAnimations(registry, deltaTime);
        InvokeCustomUpdateCallbacks(registry, deltaTime);
        ResolveTransforms(registry);
        ResolveDepth(registry);
        UpdateInput(registry);
        OptionsSystem::Update(registry);
        ShaderSystem::UpdateCompile(registry);
        InvokeCustomInputCallbacks(registry);
        UpdateText(registry);
        UpdateImageLoader(registry);
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
                    
                    myMin.y += WindowHeaderSystem::GetContentTopInset(registry, entity);

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
        InputStateSystem::Update(registry, DebugMode, s_SelectedEntity);
    }

    void UIRenderer::UpdateText(entt::registry& registry) {
        TextInputSystem::Update(registry);
    }

    void UIRenderer::UpdateAnimations(entt::registry& registry, float deltaTime) {
        AnimationSystem::Update(registry, deltaTime);
    }

    void UIRenderer::Render(entt::registry& registry) {
        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
        ShaderSystem::ClearQueue();
        
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
            ImU32 gradientTopColor = style.GradientTopColor;
            ImU32 gradientBottomColor = style.GradientBottomColor;
            ImU32 outlineColor = style.OutlineColor;
            float entityAlpha = 1.0f;
            
            if (registry.any_of<TransparencyComponent>(entity)) {
                entityAlpha = Clamp01(registry.get<TransparencyComponent>(entity).Alpha);
                bgColor = ScaleColorAlpha(bgColor, entityAlpha);
                borderColor = ScaleColorAlpha(borderColor, entityAlpha);
                gradientTopColor = ScaleColorAlpha(gradientTopColor, entityAlpha);
                gradientBottomColor = ScaleColorAlpha(gradientBottomColor, entityAlpha);
                outlineColor = ScaleColorAlpha(outlineColor, entityAlpha);
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
                        clipMin.y += WindowHeaderSystem::GetContentTopInset(registry, parent);
                        
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
                    ImVec2 selfClipMin = p_min;
                    selfClipMin.y += WindowHeaderSystem::GetContentTopInset(registry, entity);
                    draw_list->PushClipRect(selfClipMin, p_max, true);
                    pushedClip = true;
                }
            }

            // --- Render Shadow ---
            if (registry.any_of<ShadowComponent>(entity)) {
                const auto& shadow = registry.get<ShadowComponent>(entity);
                bool queuedShadow = false;
                if (shadow.RenderMode == ShadowRenderMode::Shader) {
                    queuedShadow = ShaderSystem::TryQueueEntityImGui(
                        registry,
                        entity,
                        draw_list,
                        p_min,
                        p_max,
                        style.ZIndexInt,
                        entityAlpha,
                        false,
                        true);
                }
                if (!queuedShadow) {
                    ShadowSystem::Draw(shadow, draw_list, p_min, p_max, style.Rounding, style.RoundingFlags, entityAlpha);
                }
            }

            // --- Render Glow ---
            if (registry.any_of<GlowComponent>(entity)) {
                const auto& glow = registry.get<GlowComponent>(entity);
                bool queuedGlow = false;
                if (glow.RenderMode == GlowRenderMode::Shader) {
                    queuedGlow = ShaderSystem::TryQueueEntityImGui(
                        registry,
                        entity,
                        draw_list,
                        p_min,
                        p_max,
                        style.ZIndexInt,
                        entityAlpha,
                        true,
                        false);
                }

                if (!queuedGlow && glow.Enabled && glow.Intensity > 0.0f) {
                    const int sampleCount = (std::max)(2, glow.GetEffectiveSampleCount());
                    if (sampleCount > 0) {
                        const int r = (glow.Color >> 0) & 0xFF;
                        const int g = (glow.Color >> 8) & 0xFF;
                        const int b = (glow.Color >> 16) & 0xFF;
                        const float colorAlpha = static_cast<float>((glow.Color >> 24) & 0xFF) / 255.0f;
                        const float intensity = (std::max)(0.0f, (std::min)(glow.Intensity, 8.0f));
                        const float qualityRadiusScale =
                            glow.QualityMode == GlowQualityMode::Performance ? 0.84f :
                            glow.QualityMode == GlowQualityMode::Balanced ? 1.00f : 1.14f;
                        const float modeRadiusScale =
                            glow.Mode == GlowMode::AmbientSoft ? 1.30f :
                            glow.Mode == GlowMode::NeonTube ? 0.90f : 1.00f;
                        const float glowRadius = (std::max)(
                            0.0f,
                            (std::min)(glow.Radius * glow.RadiusScale * modeRadiusScale * qualityRadiusScale, 240.0f));
                        const float falloff = (std::max)(0.2f, (std::min)(glow.Falloff, 4.0f));
                        const float coreStrength = (std::max)(0.0f, (std::min)(glow.CoreStrength, 2.0f));
                        const float modeEnergyScale =
                            glow.Mode == GlowMode::AmbientSoft ? 0.74f :
                            glow.Mode == GlowMode::NeonTube ? 1.16f : 1.0f;
                        const float qualityEnergyScale =
                            glow.QualityMode == GlowQualityMode::Performance ? 0.80f :
                            glow.QualityMode == GlowQualityMode::Balanced ? 1.0f : 1.18f;
                        const float alphaBudget = colorAlpha * entityAlpha * intensity * modeEnergyScale * qualityEnergyScale;
                        const auto& kernelDistances = glow.GetKernelDistances();
                        const auto& kernelWeights = glow.GetKernelWeights();
                        const float maxLayerAlpha =
                            glow.QualityMode == GlowQualityMode::Performance ? 54.0f :
                            glow.QualityMode == GlowQualityMode::Balanced ? 84.0f : 116.0f;

                        for (int i = sampleCount - 1; i >= 0; --i) {
                            const float distanceNorm = i < static_cast<int>(kernelDistances.size())
                                ? kernelDistances[static_cast<size_t>(i)]
                                : static_cast<float>(i + 1) / static_cast<float>(sampleCount);
                            const float dist = glowRadius * distanceNorm;
                            const float weight = i < static_cast<int>(kernelWeights.size())
                                ? kernelWeights[static_cast<size_t>(i)]
                                : (1.0f / static_cast<float>(sampleCount));

                            float modeLayerScale = 1.0f;
                            if (glow.Mode == GlowMode::NeonTube) {
                                modeLayerScale = 1.36f - distanceNorm * 0.58f;
                            } else if (glow.Mode == GlowMode::AmbientSoft) {
                                modeLayerScale = 0.88f - distanceNorm * 0.12f;
                            }
                            if (modeLayerScale < 0.32f) {
                                modeLayerScale = 0.32f;
                            }

                            float alphaLayer = alphaBudget * weight * static_cast<float>(sampleCount) * 84.0f;
                            alphaLayer *= modeLayerScale;
                            alphaLayer *= (1.0f + 0.10f * (1.0f / falloff));
                            if (alphaLayer < 0.5f) {
                                continue;
                            }
                            const float layerCap = maxLayerAlpha * (0.72f + (1.0f - distanceNorm) * 0.28f);
                            if (alphaLayer > layerCap) {
                                alphaLayer = layerCap;
                            }

                            draw_list->AddRectFilled(
                                ImVec2(p_min.x - dist, p_min.y - dist),
                                ImVec2(p_max.x + dist, p_max.y + dist),
                                IM_COL32(r, g, b, static_cast<int>(alphaLayer)),
                                style.Rounding + dist,
                                style.RoundingFlags);
                        }

                        if (!glow.OuterOnly) {
                            const float coreAlphaScale =
                                glow.Mode == GlowMode::NeonTube ? 130.0f :
                                glow.Mode == GlowMode::AmbientSoft ? 86.0f : 104.0f;
                            const float coreAlpha = (std::max)(
                                0.0f,
                                (std::min)(132.0f, alphaBudget * coreStrength * coreAlphaScale));
                            if (coreAlpha > 0.5f) {
                                const float coreInset = (std::max)(0.0f, (std::min)(3.5f, 2.0f - coreStrength * 0.4f));
                                draw_list->AddRectFilled(
                                    ImVec2(p_min.x + coreInset, p_min.y + coreInset),
                                    ImVec2(p_max.x - coreInset, p_max.y - coreInset),
                                    IM_COL32(r, g, b, static_cast<int>(coreAlpha)),
                                    (std::max)(0.0f, style.Rounding - coreInset),
                                    style.RoundingFlags);
                            }

                            if (glow.InnerGlow) {
                                const float innerAlpha = (std::max)(0.0f, (std::min)(96.0f, alphaBudget * 60.0f));
                                if (innerAlpha > 0.5f) {
                                    const float inset = 1.5f;
                                    draw_list->AddRectFilled(
                                        ImVec2(p_min.x + inset, p_min.y + inset),
                                        ImVec2(p_max.x - inset, p_max.y - inset),
                                        IM_COL32(r, g, b, static_cast<int>(innerAlpha)),
                                        (std::max)(0.0f, style.Rounding - inset),
                                        style.RoundingFlags);
                                }
                            }
                        }
                    }
                }
            }

            // Draw Background
            if ((bgColor & IM_COL32_A_MASK) != 0) {
                if (style.UseGradient) {
                    draw_list->AddRectFilledMultiColor(
                        p_min,
                        p_max,
                        gradientTopColor,
                        gradientTopColor,
                        gradientBottomColor,
                        gradientBottomColor);
                } else {
                    draw_list->AddRectFilled(p_min, p_max, bgColor, style.Rounding, style.RoundingFlags);
                }
            }

            WindowHeaderSystem::DrawHeader(registry, entity, draw_list, p_min, p_max, style.Rounding, style.RoundingFlags, entityAlpha);

            // Draw Border
            if (style.BorderSize > 0.0f && (borderColor & IM_COL32_A_MASK) != 0) {
                draw_list->AddRect(p_min, p_max, borderColor, style.Rounding, style.RoundingFlags, style.BorderSize);
            }
            if (style.OutlineEnabled && style.OutlineThickness > 0.0f && (outlineColor & IM_COL32_A_MASK) != 0) {
                const float inset = style.BorderSize + style.OutlineThickness * 0.5f;
                draw_list->AddRect(
                    ImVec2(p_min.x - inset, p_min.y - inset),
                    ImVec2(p_max.x + inset, p_max.y + inset),
                    outlineColor,
                    style.Rounding + inset,
                    style.RoundingFlags,
                    style.OutlineThickness);
            }

            ShapeSystem::Draw(registry, entity, draw_list, p_min, p_max, true);

            // Image content rendering (within entity clip/header/padding constraints).
            if (registry.any_of<Components::ImageLoader>(entity)) {
                const auto& loader = registry.get<Components::ImageLoader>(entity);
                const float topInset = WindowHeaderSystem::GetContentTopInset(registry, entity);

                ImVec2 imageMin = ImVec2(
                    p_min.x + style.ContentPaddingX,
                    p_min.y + style.ContentPaddingY + topInset);
                ImVec2 imageMax = ImVec2(
                    p_max.x - style.ContentPaddingX,
                    p_max.y - style.ContentPaddingY);

                if (imageMax.x <= imageMin.x + 2.0f || imageMax.y <= imageMin.y + 2.0f) {
                    imageMin = p_min;
                    imageMax = p_max;
                }

                bool drawnImage = false;
                if (loader.state == Components::ImageLoader::LoadState::Loaded && loader.texture &&
                    DX12Init::g_pd3dSrvDescHeap && DX12Init::g_pd3dDevice) {
                    constexpr int kImageSrvStart = 10; // Reserved in ImageLoaderSystem.
                    const int textureIndex = static_cast<int>(reinterpret_cast<intptr_t>(loader.texture));
                    const int maxDescriptors = static_cast<int>(DX12Init::g_pd3dSrvDescHeap->GetDesc().NumDescriptors);
                    if (textureIndex >= kImageSrvStart && textureIndex < maxDescriptors) {
                        D3D12_GPU_DESCRIPTOR_HANDLE texture_handle = DX12Init::GetGpuSrvHandle(textureIndex);
                        if (texture_handle.ptr != 0) {
                            draw_list->AddImage(
                                (ImTextureID)texture_handle.ptr,
                                imageMin,
                                imageMax,
                                ImVec2(0, 0),
                                ImVec2(1, 1),
                                ScaleColorAlpha(IM_COL32(255, 255, 255, 255), entityAlpha));
                            drawnImage = true;
                        }
                    }
                }

                if (!drawnImage && (loader.state == Components::ImageLoader::LoadState::Loading ||
                           loader.state == Components::ImageLoader::LoadState::Idle)) {
                    draw_list->AddRectFilled(imageMin, imageMax, ScaleColorAlpha(IM_COL32(55, 55, 65, 255), entityAlpha), 6.0f);
                    draw_list->AddRect(imageMin, imageMax, ScaleColorAlpha(IM_COL32(95, 95, 110, 255), entityAlpha), 6.0f);
                    const char* loadingText = loader.LastStatusMessage.empty()
                        ? "Loading image..."
                        : loader.LastStatusMessage.c_str();
                    ImVec2 textSize = ImGui::CalcTextSize(loadingText);
                    ImVec2 textPos = ImVec2(
                        imageMin.x + ((imageMax.x - imageMin.x) - textSize.x) * 0.5f,
                        imageMin.y + ((imageMax.y - imageMin.y) - textSize.y) * 0.5f
                    );
                    draw_list->AddText(textPos, ScaleColorAlpha(IM_COL32(210, 210, 225, 255), entityAlpha), loadingText);
                } else if (!drawnImage) {
                    draw_list->AddRectFilled(imageMin, imageMax, ScaleColorAlpha(IM_COL32(65, 35, 35, 255), entityAlpha), 6.0f);
                    draw_list->AddRect(imageMin, imageMax, ScaleColorAlpha(IM_COL32(155, 70, 70, 255), entityAlpha), 6.0f);
                    const char* failText = loader.LastStatusMessage.empty()
                        ? "Image failed"
                        : loader.LastStatusMessage.c_str();
                    ImVec2 textSize = ImGui::CalcTextSize(failText);
                    ImVec2 textPos = ImVec2(
                        imageMin.x + ((imageMax.x - imageMin.x) - textSize.x) * 0.5f,
                        imageMin.y + ((imageMax.y - imageMin.y) - textSize.y) * 0.5f
                    );
                    draw_list->AddText(textPos, ScaleColorAlpha(IM_COL32(245, 190, 190, 255), entityAlpha), failText);
                }
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
                const float debugTopInset = WindowHeaderSystem::GetContentTopInset(registry, entity);
                auto lines = TextLayout::CalculateTextLines(
                    text,
                    transform,
                    style.ContentPaddingX,
                    style.ContentPaddingY,
                    debugTopInset);
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
                    const float topInset = WindowHeaderSystem::GetContentTopInset(registry, entity);
                    ImVec2 text_start_pos = ImVec2(
                        p_min.x + style.ContentPaddingX,
                        p_min.y + style.ContentPaddingY + topInset);
                    float availableWidth = transform.Size.x - style.ContentPaddingX * 2.0f;
                    if (availableWidth < 1.0f) {
                        availableWidth = 1.0f;
                    }
                    
                    // Apply Scroll Offset if present on the same entity
                    if (registry.any_of<ScrollComponent>(entity)) {
                        text_start_pos.y -= registry.get<ScrollComponent>(entity).ScrollY;
                    }

                    if (textComp.Clip()) {
                        draw_list->PushClipRect(
                            ImVec2(p_min.x + style.ContentPaddingX, p_min.y + topInset),
                            p_max,
                            true);
                    }

                    int charIndexCounter = 0;

                    auto DrawTextSpan = [&](const std::string& str, const ImVec2& pos, ImU32 col, bool bold) {
                        const ImU32 spanColor = ScaleColorAlpha(col, entityAlpha);
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
                                 ImU32 charColor = spanColor;
                                 float scale = 1.0f;

                                 // Callback
                                 textComp.CharacterTransformCallback(charIndexCounter, c, charPos, rotation, charColor, scale);
                                 charColor = ScaleColorAlpha(charColor, entityAlpha);
                                 
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
                                draw_list->AddText(ImVec2(pos.x + 1, pos.y), spanColor, str.c_str());
                                draw_list->AddText(pos, spanColor, str.c_str());
                            } else {
                                draw_list->AddText(pos, spanColor, str.c_str());
                            }
                            charIndexCounter += (int)str.length();
                        }
                    };

                    ImVec2 cursor = text_start_pos;
                    float startX = text_start_pos.x;
                    
                    // Use shared layout logic
                    std::vector<TextLayout::TextLayoutLine> lines = TextLayout::CalculateLayout(textComp, availableWidth);

                    // Update Scroll Content Height dynamically
                    if (registry.any_of<ScrollComponent>(entity)) {
                        float totalTextHeight = 0.0f;
                        for (const auto& line : lines) {
                            totalTextHeight += line.Height;
                        }
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
                            xOffset = (availableWidth - line.Width) * 0.5f;
                        } else if (textComp.Alignment == TextAlign::Right) {
                            xOffset = availableWidth - line.Width;
                        } else if (textComp.Alignment == TextAlign::Justify) {
                             // Only justify if not the last line
                             if (i < lines.size() - 1 && line.Spans.size() > 1) {
                                 float totalExtra = availableWidth - line.Width;
                                 if (totalExtra > 0) {
                                     extraSpacing = totalExtra / (float)(line.Spans.size() - 1);
                                 }
                             }
                        }

                        float currentX = startX + xOffset;
                        for (const auto& span : line.Spans) {
                            DrawTextSpan(span.Text, ImVec2(currentX, cursor.y), span.Color, span.Bold);
                            currentX += span.Size.x;
                            if (textComp.Alignment == TextAlign::Justify) {
                                currentX += extraSpacing;
                            }
                        }
                        cursor.y += line.Height;
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
                
                if (custom.Enabled && custom.OnRender) {
                    custom.OnRender(registry, entity, draw_list, p_min, p_max, isHovered, isClicked);
                }
            }

            // Slider
            if (registry.any_of<SliderComponent>(entity)) {
                auto& slider = registry.get<SliderComponent>(entity);
                const float sliderRange = slider.Max - slider.Min;
                
                // Logic
                if (slider.IsDragging) {
                    float mouseX = ImGui::GetMousePos().x;
                    float trackStart = p_min.x;
                    float trackWidth = p_max.x - p_min.x;
                    if (trackWidth <= 0.0f) {
                        slider.IsDragging = false;
                    } else {
                    
                        // Interaction Sensitivity (simple approach: just standard mapping for now, but could be scaled)
                        // "Scroll per area" requested - usually means scaling the delta, but absolute positioning is standard for sliders.
                        // If sensitivity != 1.0, we might need relative drag mode.
                        // For now, let's keep absolute mapping as it's most intuitive for standard sliders.
                        // If user wants "sensitivity", it usually applies to infinite sliders or knobs.
                        // We'll stick to 1:1 mapping for the bar, but maybe smooth the result.

                        float normalized = (mouseX - trackStart) / trackWidth;
                        if (normalized < 0.0f) normalized = 0.0f;
                        if (normalized > 1.0f) normalized = 1.0f;

                        float newValue = slider.Min + normalized * sliderRange;

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
                draw_list->AddRectFilled(
                    ImVec2(p_min.x, trackY),
                    ImVec2(p_max.x, trackY + trackH),
                    ScaleColorAlpha(slider.ColorTrack, entityAlpha),
                    2.0f);
                
                // Fill
                float fillRatio = 0.0f;
                if (sliderRange > 0.0f) {
                    fillRatio = (slider.VisualValue - slider.Min) / sliderRange;
                }
                if (fillRatio < 0.0f) fillRatio = 0.0f;
                if (fillRatio > 1.0f) fillRatio = 1.0f;
                
                float fillWidth = fillRatio * (p_max.x - p_min.x);
                draw_list->AddRectFilled(
                    ImVec2(p_min.x, trackY),
                    ImVec2(p_min.x + fillWidth, trackY + trackH),
                    ScaleColorAlpha(slider.ColorFill, entityAlpha),
                    2.0f);
                
                // Knob
                float knobX = p_min.x + fillWidth;
                draw_list->AddCircleFilled(
                    ImVec2(knobX, trackY + trackH * 0.5f),
                    slider.KnobRadius,
                    ScaleColorAlpha(slider.ColorKnob, entityAlpha));
                
                // Hover Effect (Outer Glow)
                if (registry.any_of<InputStateComponent>(entity) && registry.get<InputStateComponent>(entity).IsHovered) {
                     draw_list->AddCircle(
                        ImVec2(knobX, trackY + trackH * 0.5f),
                        slider.KnobRadius + 1.0f,
                        ScaleColorAlpha(IM_COL32(200, 200, 255, 100), entityAlpha),
                        12,
                        2.0f);
                }
            }

            // TextInput
            if (registry.any_of<TextInputComponent>(entity)) {
                auto& textInput = registry.get<TextInputComponent>(entity);
                bool focused = textInput.IsFocused;
                ImU32 bgCol = focused ? IM_COL32(50, 50, 60, 255) : IM_COL32(40, 40, 45, 255);
                draw_list->AddRectFilled(p_min, p_max, ScaleColorAlpha(bgCol, entityAlpha), 4.0f);
                draw_list->AddRect(
                    p_min,
                    p_max,
                    ScaleColorAlpha(focused ? IM_COL32(100, 150, 255, 255) : IM_COL32(80, 80, 90, 255), entityAlpha),
                    4.0f);
                draw_list->PushClipRect(ImVec2(p_min.x + 4, p_min.y), ImVec2(p_max.x - 4, p_max.y), true);
                const char* displayStr = textInput.Buffer.empty() ? textInput.Placeholder.c_str() : textInput.Buffer.c_str();
                ImU32 textCol = textInput.Buffer.empty() ? IM_COL32(150, 150, 150, 255) : IM_COL32(255, 255, 255, 255);
                float textH = ImGui::GetFontSize();
                float textY = p_min.y + (p_max.y - p_min.y) * 0.5f - textH * 0.5f;
                ImVec2 textPos = ImVec2(p_min.x + 5, textY);
                draw_list->AddText(textPos, ScaleColorAlpha(textCol, entityAlpha), displayStr);
                if (focused && (int(ImGui::GetTime() * 2) % 2 == 0)) {
                    // Correctly position cursor based on CursorPos index
                    // (std::min) prevents macro expansion on Windows
                    std::string sub = textInput.Buffer.substr(0, (std::min)((size_t)textInput.CursorPos, textInput.Buffer.length()));
                    ImVec2 subSize = ImGui::CalcTextSize(sub.c_str());
                    float cursorX = textPos.x + subSize.x;
                    draw_list->AddLine(
                        ImVec2(cursorX, p_min.y + 4),
                        ImVec2(cursorX, p_max.y - 4),
                        ScaleColorAlpha(IM_COL32(255, 255, 255, 255), entityAlpha));
                }
                draw_list->PopClipRect();
            }

            // Options (segmented selection control)
            if (registry.any_of<OptionsComponent>(entity)) {
                auto& options = registry.get<OptionsComponent>(entity);
                const int optionCount = static_cast<int>(options.Options.size());
                if (optionCount > 0) {
                    const float topInset = WindowHeaderSystem::GetContentTopInset(registry, entity);
                    ImVec2 optionMin = ImVec2(
                        p_min.x + style.ContentPaddingX,
                        p_min.y + style.ContentPaddingY + topInset);
                    ImVec2 optionMax = ImVec2(
                        p_max.x - style.ContentPaddingX,
                        p_max.y - style.ContentPaddingY);
                    if (optionMax.x <= optionMin.x + 4.0f || optionMax.y <= optionMin.y + 4.0f) {
                        optionMin = p_min;
                        optionMax = p_max;
                    }

                    int selected = options.SelectedIndex;
                    if (selected < 0) selected = 0;
                    if (selected >= optionCount) selected = optionCount - 1;
                    options.SelectedIndex = selected;

                    const float width = optionMax.x - optionMin.x;
                    const float height = optionMax.y - optionMin.y;
                    if (width > 1.0f && height > 1.0f) {
                        const float rounding = (std::max)(3.0f, (std::min)(style.Rounding, 8.0f));
                        draw_list->AddRectFilled(optionMin, optionMax, IM_COL32(38, 42, 50, static_cast<int>(220.0f * entityAlpha)), rounding);
                        draw_list->AddRect(optionMin, optionMax, IM_COL32(90, 95, 110, static_cast<int>(180.0f * entityAlpha)), rounding, 0, 1.0f);

                        const float segmentWidth = width / static_cast<float>(optionCount);
                        for (int i = 0; i < optionCount; ++i) {
                            const ImVec2 segMin = ImVec2(optionMin.x + segmentWidth * static_cast<float>(i), optionMin.y);
                            const ImVec2 segMax = ImVec2(segMin.x + segmentWidth, optionMax.y);
                            const bool isSelected = i == selected;
                            if (isSelected) {
                                draw_list->AddRectFilled(segMin, segMax, IM_COL32(92, 145, 255, static_cast<int>(195.0f * entityAlpha)), rounding);
                            } else if (i > 0) {
                                draw_list->AddLine(
                                    ImVec2(segMin.x, segMin.y + 2.0f),
                                    ImVec2(segMin.x, segMax.y - 2.0f),
                                    IM_COL32(80, 86, 100, static_cast<int>(170.0f * entityAlpha)),
                                    1.0f);
                            }

                            const std::string& label = options.Options[static_cast<size_t>(i)];
                            ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                            ImVec2 textPos = ImVec2(
                                segMin.x + (segmentWidth - textSize.x) * 0.5f,
                                segMin.y + (height - textSize.y) * 0.5f
                            );
                            draw_list->AddText(
                                textPos,
                                isSelected
                                    ? IM_COL32(255, 255, 255, static_cast<int>(245.0f * entityAlpha))
                                    : IM_COL32(200, 210, 220, static_cast<int>(220.0f * entityAlpha)),
                                label.c_str());
                        }
                    }
                }
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
                        draw_list->AddRectFilled(trackMin, trackMax, ScaleColorAlpha(IM_COL32(30, 30, 35, 200), entityAlpha), 4.0f);
                        float trackH = trackMax.y - trackMin.y;
                        if (trackH > 0) {
                            float thumbH = trackH * (scroll.ViewHeight / scroll.ContentHeight);
                            if (thumbH < 15) thumbH = 15;
                            if (thumbH > trackH) thumbH = trackH;
                            float maxScroll = scroll.ContentHeight - scroll.ViewHeight;
                            float ratio = (maxScroll > 0) ? (scroll.ScrollY / maxScroll) : 0;
                            if (ratio < 0) ratio = 0; if (ratio > 1) ratio = 1;
                            float thumbY = trackMin.y + ratio * (trackH - thumbH);
                            draw_list->AddRectFilled(
                                ImVec2(trackMin.x + 1, thumbY + 1),
                                ImVec2(trackMax.x - 1, thumbY + thumbH - 1),
                                ScaleColorAlpha(IM_COL32(100, 100, 120, 255), entityAlpha),
                                4.0f);
                        }
                     }
                }
            }

            ShapeSystem::Draw(registry, entity, draw_list, p_min, p_max, false);

            if (pushedClip) {
                draw_list->PopClipRect();
            }

            if (parentClipped) {
                draw_list->PopClipRect();
            }

            // Handle Rotation (Post-Draw Vertex Transformation)
            if (transform.Rotation != 0.0f) {
                int vtx_idx_end = draw_list->_VtxCurrentIdx;
                ImVec2 center = ImVec2(p_min.x + transform.Size.x * 0.5f, p_min.y + transform.Size.y * 0.5f);
                RotateVertices(draw_list, vtx_idx_start, vtx_idx_end, center, transform.Rotation);
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
        return TextLayout::CalculateTextLines(text, transform, container);
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

    // --- Image Loader Implementation ---

    void UIRenderer::UpdateImageLoader(entt::registry& registry) {
        Components::ImageLoaderSystem::Update(registry);
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
