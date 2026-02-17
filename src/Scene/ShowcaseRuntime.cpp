#include "Scene/ShowcaseRuntime.hpp"
#include "Dx12Init/Dx12Init.hpp"
#include "Render/RenderUtils/UIRenderer.hpp"
#include "Render/RenderUtils/UIComponents.hpp"
#include "Render/RenderUtils/UIBuilder.hpp"
#include "Render/RenderUtils/FontSystem.hpp"
#include "Render/RenderUtils/ShaderSystem.hpp"

#include "imgui.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace Scene {

    namespace {
        entt::registry g_registry;
        ShowcaseRuntime::Options g_RuntimeOptions;

        enum class DemoTab {
            Overview = 0,
            Input = 1,
            Effects = 2,
            Media = 3,
            Interaction = 4,
            Systems = 5
        };

        bool g_RequestStartupRestart = false;
        bool g_RequestShaderRecompile = false;
        RenderUtils::StartupRuntime::Config startupConfig;
        RenderUtils::StartupRuntime::State startupState;

        bool IsHttpSourceValue(const std::string& value) {
            return value.rfind("https://", 0) == 0 || value.rfind("http://", 0) == 0;
        }

        void ApplyRuntimeFeatureRestrictions(entt::registry& registry) {
            if (!g_RuntimeOptions.EnableShaderLoading) {
                auto shaderView = registry.view<RenderUtils::ShaderComponent>();
                std::vector<entt::entity> shaderEntities;
                for (auto entity : shaderView) {
                    shaderEntities.push_back(entity);
                }
                for (auto entity : shaderEntities) {
                    registry.remove<RenderUtils::ShaderComponent>(entity);
                }

                auto glowView = registry.view<RenderUtils::GlowComponent>();
                for (auto entity : glowView) {
                    auto& glow = glowView.get<RenderUtils::GlowComponent>(entity);
                    glow.SetRenderMode(RenderUtils::GlowRenderMode::Cpu);
                }

                auto shadowView = registry.view<RenderUtils::ShadowComponent>();
                for (auto entity : shadowView) {
                    auto& shadow = shadowView.get<RenderUtils::ShadowComponent>(entity);
                    shadow.SetRenderMode(RenderUtils::ShadowRenderMode::Cpu);
                }

                const entt::entity shaderCard = RenderUtils::UIRenderer::FindEntityByName(registry, "SystemsShaderCard");
                if (registry.valid(shaderCard) && registry.any_of<RenderUtils::StyleComponent>(shaderCard)) {
                    registry.get<RenderUtils::StyleComponent>(shaderCard).Visible = false;
                }

                const entt::entity glowModeOptions = RenderUtils::UIRenderer::FindEntityByName(registry, "SysGlowRenderModeOptions");
                if (registry.valid(glowModeOptions) && registry.any_of<RenderUtils::OptionsComponent>(glowModeOptions)) {
                    registry.get<RenderUtils::OptionsComponent>(glowModeOptions).SelectedIndex = 1;
                }

                const entt::entity shadowModeOptions = RenderUtils::UIRenderer::FindEntityByName(registry, "SysShadowRenderModeOptions");
                if (registry.valid(shadowModeOptions) && registry.any_of<RenderUtils::OptionsComponent>(shadowModeOptions)) {
                    registry.get<RenderUtils::OptionsComponent>(shadowModeOptions).SelectedIndex = 0;
                }
            }

            if (g_RuntimeOptions.RelaxRequiredFonts) {
                auto fontsView = registry.view<RenderUtils::FontsComponent>();
                for (auto entity : fontsView) {
                    auto& fonts = fontsView.get<RenderUtils::FontsComponent>(entity);
                    for (auto& face : fonts.Faces) {
                        face.StartupRequired = false;
                    }
                }
            }

            if (!g_RuntimeOptions.EnableImageLoading) {
                auto imageView = registry.view<Components::ImageLoader>();
                std::vector<entt::entity> imageEntities;
                for (auto entity : imageView) {
                    imageEntities.push_back(entity);
                }

                for (auto entity : imageEntities) {
                    RenderUtils::UIRenderer::FreeImageResource(registry, entity);
                    if (registry.valid(entity) && registry.any_of<Components::ImageLoader>(entity)) {
                        registry.remove<Components::ImageLoader>(entity);
                    }
                }
                return;
            }

            bool requestImageRestart = false;
            auto imageView = registry.view<Components::ImageLoader>();
            for (auto entity : imageView) {
                auto& loader = imageView.get<Components::ImageLoader>(entity);
                if (g_RuntimeOptions.RelaxRequiredImages && loader.StartupRequired) {
                    loader.StartupRequired = false;
                }

                if (!g_RuntimeOptions.StripLocalPathMediaSources || loader.Sources.empty()) {
                    continue;
                }

                std::vector<Components::ImageLoader::ImageSource> filteredSources;
                filteredSources.reserve(loader.Sources.size());
                for (const auto& source : loader.Sources) {
                    if (source.Type == Components::ImageLoader::ImageSourceType::Url || IsHttpSourceValue(source.Value)) {
                        filteredSources.push_back(source);
                    }
                }

                if (filteredSources.size() == loader.Sources.size()) {
                    continue;
                }

                loader.Sources = std::move(filteredSources);
                if (loader.ActiveSourceIndex < 0 || loader.ActiveSourceIndex >= static_cast<int>(loader.Sources.size())) {
                    loader.ActiveSourceIndex = 0;
                }
                requestImageRestart = true;
            }

            if (requestImageRestart) {
                Components::ImageLoaderSystem::RequestRestart(registry, false);
            }
        }
    }

    void ShowcaseRuntime::SetOptions(const ShowcaseRuntime::Options& options) {
        g_RuntimeOptions = options;
    }


    void ShowcaseRuntime::Setup(HWND hWnd) {
        (void)hWnd;
            g_registry.clear();
            g_RequestStartupRestart = false;
            g_RequestShaderRecompile = false;
            startupConfig = RenderUtils::StartupRuntime::Config{};
            RenderUtils::StartupRuntime::Reset(startupState);
            if (g_RuntimeOptions.ForceImmediateStartup) {
                startupConfig.ModeValue = RenderUtils::StartupRuntime::Mode::ImmediateUI;
                startupState.Initialized = true;
                startupState.Completed = true;
                startupState.TimedOut = false;
                startupState.StartTimeSec = ImGui::GetCurrentContext() ? static_cast<float>(ImGui::GetTime()) : 0.0f;
                startupState.ElapsedSec = 0.0f;
                startupState.SummaryLine = "Startup forced to Immediate UI profile.";
            }

            RenderUtils::UIRenderer::Init(g_registry);
            RenderUtils::ShaderSystem::RegisterUniformResolver(
            "app.mouse_norm",
            [](const RenderUtils::ShaderAutoUniformContext& context,
                RenderUtils::ShaderParamType expectedType,
                RenderUtils::ShaderParamValue& outValue,
                std::string& outError) -> bool {
                const float safeW = (std::max)(1.0f, context.DisplaySize.x);
                const float safeH = (std::max)(1.0f, context.DisplaySize.y);
                const ImVec2 mouseNorm = ImVec2(context.MousePos.x / safeW, context.MousePos.y / safeH);
                if (expectedType == RenderUtils::ShaderParamType::Vec2) {
                    outValue = mouseNorm;
                    return true;
                }
                if (expectedType == RenderUtils::ShaderParamType::Vec4) {
                    outValue = ImVec4(mouseNorm.x, mouseNorm.y, 0.0f, 0.0f);
                    return true;
                }
                outError = "Uniform 'app.mouse_norm' expects Vec2 or Vec4.";
                return false;
            });
            RenderUtils::ShaderSystem::RegisterUniformResolver(
            "app.sin_time",
            [](const RenderUtils::ShaderAutoUniformContext& context,
                RenderUtils::ShaderParamType expectedType,
                RenderUtils::ShaderParamValue& outValue,
                std::string& outError) -> bool {
                if (expectedType != RenderUtils::ShaderParamType::Float) {
                    outError = "Uniform 'app.sin_time' expects Float.";
                    return false;
                }
                outValue = std::sin(context.TimeSeconds);
                return true;
            });

            // --- UI Construction ---
            RenderUtils::UIRenderer::SetCurrentTab(static_cast<int>(DemoTab::Overview));
            RenderUtils::UIBuilder::Begin(g_registry)
            .Create<RenderUtils::ContainerType::Panel>("MainContainer")
                .With<RenderUtils::TransformComponent>(
                    RenderUtils::TransformComponent().SetPosition(ImVec2(0, 0)).SetSize(ImVec2(1600, 900))
                )
                .With<RenderUtils::StyleComponent>(
                    RenderUtils::StyleComponent()
                        .SetBackgroundColor(IM_COL32(0, 0, 0, 0))
                        .SetGradient(false, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0))
                        .SetLayer(RenderUtils::ZOrder::Background)
                )
                .With<RenderUtils::InputStateComponent>(RenderUtils::InputStateComponent(false, false, false).SetBlockInput(false))
                .With<RenderUtils::LockedComponent>(RenderUtils::LockedComponent().SetLocked(true))
            .End()

            .Create<RenderUtils::ContainerType::Window>("ModMenuShowcase")
                .With<RenderUtils::TransformComponent>(
                    RenderUtils::TransformComponent().SetPosition(ImVec2(60, 40)).SetSize(ImVec2(1480, 810))
                )
                .With<RenderUtils::StyleComponent>(
                    RenderUtils::StyleComponent()
                        .SetBackgroundColor(IM_COL32(30, 33, 40, 255))
                        .SetBorderColor(IM_COL32(72, 78, 95, 255))
                        .SetBorderSize(1.0f)
                        .SetRounding(11.0f)
                        .SetOutline(true, IM_COL32(90, 130, 200, 90), 1.0f)
                )
                .With<RenderUtils::WindowHeaderComponent>(
                    RenderUtils::WindowHeaderComponent()
                        .SetHeight(36.0f)
                        .SetPadding(12.0f, 10.0f)
                        .SetBackgroundColor(IM_COL32(37, 42, 52, 255))
                        .SetSeparatorColor(IM_COL32(10, 12, 18, 190))
                )
                .With<RenderUtils::DraggableComponent>(RenderUtils::DraggableComponent().SetMode(RenderUtils::DragMode::Free))
                .With<RenderUtils::ClipComponent>(RenderUtils::ClipComponent().SetClipChildren(true))
                .With<RenderUtils::FontsComponent>([]() {
                    RenderUtils::FontsComponent fonts;
                    const bool preferRemoteBodyFace = g_RuntimeOptions.PreferRemoteBodyFont;
                    const bool relaxRequiredFonts = g_RuntimeOptions.RelaxRequiredFonts;
                    fonts
                        .SetDefaultFace(preferRemoteBodyFace ? "ui.body.web" : "ui.body")
                        .SetInheritFromParent(true)
                        .SetApplyToText(true)
                        .SetApplyToTextInput(true)
                        .SetApplyToOptions(true)
                        .SetApplyToStatusLabels(true)
                        .SetAutoReloadLocalFiles(true)
                        .SetReloadPollSeconds(1.0f);
                    fonts.AddPathFace("ui.body", "C:/Windows/Fonts/segoeui.ttf", 17.0f)
                        .SetGlyphPreset(RenderUtils::FontGlyphPreset::Default)
                        .SetStartupRequired(!relaxRequiredFonts);
                    if (preferRemoteBodyFace) {
                        fonts.AddUrlFace(
                            "ui.body.web",
                            "https://raw.githubusercontent.com/google/fonts/main/ofl/ibmplexsans/IBMPlexSans-Regular.ttf",
                            17.0f)
                            .SetGlyphPreset(RenderUtils::FontGlyphPreset::Default)
                            .SetMaxBytes(8u * 1024u * 1024u)
                            .SetRetryPolicy(2, 350.0f)
                            .SetStartupRequired(false);
                    }
                    fonts.AddPathFace("ui.mono", "C:/Windows/Fonts/consola.ttf", 16.0f)
                        .SetGlyphPreset(RenderUtils::FontGlyphPreset::Default);
                    fonts.AddUrlFace(
                        "ui.accent",
                        "https://raw.githubusercontent.com/google/fonts/main/ofl/ibmplexmono/IBMPlexMono-Regular.ttf",
                        18.0f)
                        .SetGlyphPreset(RenderUtils::FontGlyphPreset::Default)
                        .SetMaxBytes(8u * 1024u * 1024u)
                        .SetRetryPolicy(2, 350.0f)
                        .SetStartupRequired(false);
                    return fonts;
                }())

                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("Sidebar")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent().SetPosition(ImVec2(0, 36)).SetSize(ImVec2(230, 774))
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(24, 27, 34, 255)).SetBorderColor(IM_COL32(58, 63, 78, 255)).SetBorderSize(1.0f)
                        )
                        .With<RenderUtils::ClipComponent>(RenderUtils::ClipComponent().SetClipChildren(true))
                        .With<RenderUtils::TextComponent>(
                            RenderUtils::TextComponent("Renderer12 Demo Menu", IM_COL32(230, 236, 248, 255)).Align(RenderUtils::TextAlign::Center)
                        )

                        .Child(RenderUtils::UIBuilder::Begin(g_registry)
                            .Create<RenderUtils::ContainerType::Button>("TabBtnOverview")
                            .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 64)).SetSize(ImVec2(194, 40)))
                            .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(52, 56, 66, 255)).SetRounding(6.0f))
                            .IsTabTrigger((int)DemoTab::Overview, IM_COL32(88, 126, 210, 255), IM_COL32(52, 56, 66, 255))
                            .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Overview", IM_COL32(255, 255, 255, 255)).Align(RenderUtils::TextAlign::Center))
                        )
                        .Child(RenderUtils::UIBuilder::Begin(g_registry)
                            .Create<RenderUtils::ContainerType::Button>("TabBtnInput")
                            .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 112)).SetSize(ImVec2(194, 40)))
                            .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(52, 56, 66, 255)).SetRounding(6.0f))
                            .IsTabTrigger((int)DemoTab::Input, IM_COL32(88, 126, 210, 255), IM_COL32(52, 56, 66, 255))
                            .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Input", IM_COL32(255, 255, 255, 255)).Align(RenderUtils::TextAlign::Center))
                        )
                        .Child(RenderUtils::UIBuilder::Begin(g_registry)
                            .Create<RenderUtils::ContainerType::Button>("TabBtnEffects")
                            .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 160)).SetSize(ImVec2(194, 40)))
                            .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(52, 56, 66, 255)).SetRounding(6.0f))
                            .IsTabTrigger((int)DemoTab::Effects, IM_COL32(88, 126, 210, 255), IM_COL32(52, 56, 66, 255))
                            .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Effects", IM_COL32(255, 255, 255, 255)).Align(RenderUtils::TextAlign::Center))
                        )
                        .Child(RenderUtils::UIBuilder::Begin(g_registry)
                            .Create<RenderUtils::ContainerType::Button>("TabBtnMedia")
                            .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 208)).SetSize(ImVec2(194, 40)))
                            .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(52, 56, 66, 255)).SetRounding(6.0f))
                            .IsTabTrigger((int)DemoTab::Media, IM_COL32(88, 126, 210, 255), IM_COL32(52, 56, 66, 255))
                            .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Media", IM_COL32(255, 255, 255, 255)).Align(RenderUtils::TextAlign::Center))
                        )
                        .Child(RenderUtils::UIBuilder::Begin(g_registry)
                            .Create<RenderUtils::ContainerType::Button>("TabBtnInteraction")
                            .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 256)).SetSize(ImVec2(194, 40)))
                            .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(52, 56, 66, 255)).SetRounding(6.0f))
                            .IsTabTrigger((int)DemoTab::Interaction, IM_COL32(88, 126, 210, 255), IM_COL32(52, 56, 66, 255))
                            .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Interaction", IM_COL32(255, 255, 255, 255)).Align(RenderUtils::TextAlign::Center))
                        )
                        .Child(RenderUtils::UIBuilder::Begin(g_registry)
                            .Create<RenderUtils::ContainerType::Button>("TabBtnSystems")
                            .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 304)).SetSize(ImVec2(194, 40)))
                            .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(52, 56, 66, 255)).SetRounding(6.0f))
                            .IsTabTrigger((int)DemoTab::Systems, IM_COL32(88, 126, 210, 255), IM_COL32(52, 56, 66, 255))
                            .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Systems", IM_COL32(255, 255, 255, 255)).Align(RenderUtils::TextAlign::Center))
                        )
                )

                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("ContentRoot")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent().SetPosition(ImVec2(230, 36)).SetSize(ImVec2(1250, 774))
                        )
                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(0, 0, 0, 0)))
                        .With<RenderUtils::ClipComponent>(RenderUtils::ClipComponent().SetClipChildren(true))

                        // Overview
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Panel>("TabOverview")
                                .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(0, 0)).SetSize(ImVec2(1250, 774)))
                                .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetVisible(true))
                                .IsTab((int)DemoTab::Overview)
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("OverviewHero")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 18)).SetSize(ImVec2(1214, 110)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(36, 46, 68, 255)).SetGradient(true, IM_COL32(54, 72, 110, 255), IM_COL32(31, 39, 58, 255)).SetRounding(10.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("High-Fidelity Mod Menu Showcase", IM_COL32(255, 255, 255, 255))))
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("OverviewShapePanel")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 146)).SetSize(ImVec2(390, 250)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f).SetBorderColor(IM_COL32(65, 70, 84, 255)).SetBorderSize(1.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("ShapeComponent", IM_COL32(215, 225, 255, 255)))
                                    .With<RenderUtils::ShapeComponent>([](){ RenderUtils::ShapeComponent s; s.SetDrawBehindContent(false).SetClipToEntity(true).SetUseForHitTest(true); RenderUtils::ShapePrimitive r(RenderUtils::ShapeType::Rect); r.Offset=ImVec2(20,48); r.Size=ImVec2(146,96); r.Rounding=10.0f; r.Filled=true; r.FillColor=IM_COL32(93,135,220,185); r.StrokeEnabled=true; r.StrokeColor=IM_COL32(180,214,255,255); r.StrokeThickness=2.0f; RenderUtils::ShapePrimitive c(RenderUtils::ShapeType::Circle); c.Offset=ImVec2(252,95); c.Radius=46.0f; c.Filled=true; c.FillColor=IM_COL32(110,220,185,175); c.StrokeEnabled=true; c.StrokeColor=IM_COL32(182,255,232,255); c.StrokeThickness=2.0f; s.AddShape(r).AddShape(c); return s; }()))
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Window>("OverviewHeaderOff")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(836, 146)).SetSize(ImVec2(396, 250)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(44, 48, 58, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::WindowHeaderComponent>(RenderUtils::WindowHeaderComponent().SetEnabled(false))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("WindowHeader disabled", IM_COL32(220, 228, 240, 255))))
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("OverviewFontsCard")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(428, 146)).SetSize(ImVec2(390, 250)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f).SetBorderColor(IM_COL32(65, 70, 84, 255)).SetBorderSize(1.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("FontsComponent", IM_COL32(220, 230, 245, 255)).SetFont("ui.body"))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("OverviewFontsRichText")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 56)).SetSize(ImVec2(350, 106)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(0, 0, 0, 0)).SetContentPadding(0.0f, 0.0f))
                                        .With<RenderUtils::TextComponent>(
                                            RenderUtils::TextComponent("", IM_COL32(215, 228, 248, 255), RenderUtils::TextFlags::Wrap)
                                                .SetFont("ui.body")
                                                .SpanFont("Body face loaded locally. ", "ui.body", IM_COL32(215, 228, 248, 255))
                                                .SpanFont("Mono span ", "ui.mono", IM_COL32(188, 244, 255, 255), RenderUtils::TextStyle::Bold)
                                                .SpanFont("Accent span (HTTPS with fallback).", "ui.accent", IM_COL32(246, 210, 255, 255))))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("OverviewFontsCustom")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 170)).SetSize(ImVec2(350, 62)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(34, 39, 49, 255)).SetRounding(6.0f).SetBorderColor(IM_COL32(74, 88, 112, 255)).SetBorderSize(1.0f))
                                        .With<RenderUtils::CustomComponent>(
                                            RenderUtils::CustomComponent()
                                                .SetOnRender([](entt::registry& reg, entt::entity e, ImDrawList* dl, ImVec2 pMin, ImVec2, bool hovered, bool) {
                                                    ImFont* accent = RenderUtils::FontSystem::ResolveEntityFont(reg, e, "ui.accent");
                                                    ImFont* mono = RenderUtils::FontSystem::ResolveEntityFont(reg, e, "ui.mono");
                                                    RenderUtils::FontSystem::AddText(
                                                        dl,
                                                        accent,
                                                        ImVec2(pMin.x + 12, pMin.y + 12),
                                                        IM_COL32(232, 239, 255, 255),
                                                        "Custom callback uses FontSystem helper.");
                                                    if (hovered) {
                                                        RenderUtils::FontSystem::AddText(
                                                            dl,
                                                            mono,
                                                            ImVec2(pMin.x + 12, pMin.y + 34),
                                                            IM_COL32(174, 238, 255, 255),
                                                            "Hover: font resolved per-entity.");
                                                    }
                                                }))
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("OverviewFontSwitchCard")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(428, 414)).SetSize(ImVec2(390, 140)))
                                    .With<RenderUtils::StyleComponent>(
                                        RenderUtils::StyleComponent()
                                            .SetBackgroundColor(IM_COL32(40, 44, 53, 255))
                                            .SetRounding(8.0f)
                                            .SetBorderColor(IM_COL32(65, 70, 84, 255))
                                            .SetBorderSize(1.0f))
                                    .With<RenderUtils::TextComponent>(
                                        RenderUtils::TextComponent("Runtime font switch", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("OverviewFontSwitchOptions")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 54)).SetSize(ImVec2(350, 34)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetContentPadding(4.0f, 4.0f).SetRounding(6.0f))
                                        .With<RenderUtils::OptionsComponent>(RenderUtils::OptionsComponent({ "<ImGui Default>", "ui.body", "ui.mono", "ui.accent" }, 1)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("OverviewFontSwitchStatus")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 96)).SetSize(ImVec2(350, 30)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(0, 0, 0, 0)).SetContentPadding(0.0f, 0.0f))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Active: ui.body", IM_COL32(190, 208, 235, 255)).SetFont("ui.body")))
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("OverviewTransparencyBackdrop")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 414)).SetSize(ImVec2(310, 120)))
                                    .With<RenderUtils::StyleComponent>(
                                        RenderUtils::StyleComponent()
                                            .SetBackgroundColor(IM_COL32(26, 30, 38, 255))
                                            .SetGradient(true, IM_COL32(86, 92, 112, 255), IM_COL32(22, 27, 36, 255))
                                            .SetLayer(RenderUtils::ZOrder::Below)
                                            .SetRounding(8.0f)
                                            .SetBorderColor(IM_COL32(78, 88, 110, 255))
                                            .SetBorderSize(1.0f))
                                    .With<RenderUtils::TextComponent>(
                                        RenderUtils::TextComponent("Transparency backdrop", IM_COL32(198, 210, 232, 255)).Align(RenderUtils::TextAlign::Center)))
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("OverviewTransparency")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(34, 432)).SetSize(ImVec2(278, 90)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(74, 174, 255, 255)).SetLayer(RenderUtils::ZOrder::Normal).SetRounding(8.0f))
                                    .With<RenderUtils::TransparencyComponent>(RenderUtils::TransparencyComponent(0.38f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Transparency alpha: 0.38", IM_COL32(15, 24, 35, 255))))
                        )

                        // Input
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Panel>("TabInput")
                                .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(0, 0)).SetSize(ImVec2(1250, 774)))
                                .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetVisible(false))
                                .IsTab((int)DemoTab::Input)
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("InputSliderCard")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 18)).SetSize(ImVec2(390, 140)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("SliderComponent", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("InputSensitivitySlider")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 58)).SetSize(ImVec2(350, 40)))
                                        .With<RenderUtils::SliderComponent>(RenderUtils::SliderComponent(75.0f, 0.0f, 100.0f).SetSmoothing(true, 12.0f))
                                        .With<RenderUtils::InputStateComponent>(RenderUtils::InputStateComponent().SetBlockInput(false))
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("InputTextInputCard")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(428, 18)).SetSize(ImVec2(390, 140)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("TextInputComponent", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("InputConfigName")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 58)).SetSize(ImVec2(350, 40)))
                                        .With<RenderUtils::TextInputComponent>(RenderUtils::TextInputComponent("Type config name...", 48).SetBuffer("Default"))
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("InputOptionsCard")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(838, 18)).SetSize(ImVec2(394, 140)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("OptionsComponent", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("InputProfileOptions")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 56)).SetSize(ImVec2(354, 48)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetContentPadding(4.0f, 4.0f).SetRounding(6.0f))
                                        .With<RenderUtils::OptionsComponent>(RenderUtils::OptionsComponent({ "Legit", "Rage", "Hybrid" }, 0))
                                        .With<RenderUtils::InputStateComponent>(RenderUtils::InputStateComponent().SetBlockInput(false))
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("InputScrollCard")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 176)).SetSize(ImVec2(600, 286)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("ScrollComponent + ClipComponent\n\nLine 1\nLine 2\nLine 3\nLine 4\nLine 5\nLine 6\nLine 7\nLine 8\nLine 9\nLine 10\nLine 11\nLine 12", IM_COL32(220, 230, 245, 255), RenderUtils::TextFlags::Wrap | RenderUtils::TextFlags::Clip))
                                    .With<RenderUtils::ScrollComponent>(RenderUtils::ScrollComponent().SetViewHeight(286.0f).SetContentHeight(620.0f))
                                    .With<RenderUtils::ClipComponent>(RenderUtils::ClipComponent().SetClipChildren(true))
                                )
                        )

                        // Effects
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Panel>("TabEffects")
                                .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(0, 0)).SetSize(ImVec2(1250, 774)))
                                .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetVisible(false))
                                .IsTab((int)DemoTab::Effects)
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("EffectsShadowStage")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 18)).SetSize(ImVec2(610, 360)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(255, 255, 255, 255)).SetRounding(8.0f).SetBorderColor(IM_COL32(210, 210, 210, 255)).SetBorderSize(1.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("White Shadow Stage", IM_COL32(30, 30, 30, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("ShadowCardObject")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(70, 110)).SetSize(ImVec2(220, 140)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(53, 96, 180, 255)).SetRounding(12.0f))
                                        .With<RenderUtils::ShadowComponent>(RenderUtils::ShadowComponent().SetColor(IM_COL32(0, 0, 0, 135)).SetOffset(ImVec2(10, 12)).SetBlurRadius(22.0f).SetSpread(2.5f).SetSamples(22).ParamBindFloat("u_time", "time", 0.0f))
                                        .With<RenderUtils::ShaderComponent>(RenderUtils::ShaderComponent().SetPixelSource(RenderUtils::ShaderSourceSpec().SetMode(RenderUtils::ShaderSourceMode::EmbeddedCpp).SetSource("shadow.default").SetTarget("ps_5_0")))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Object A", IM_COL32(255, 255, 255, 255)))
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("EffectsGlowStage")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(648, 18)).SetSize(ImVec2(584, 360)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(24, 27, 34, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Glow Modes", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("GlowGaussianCard")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 70)).SetSize(ImVec2(170, 230)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(48, 54, 64, 255)).SetRounding(10.0f))
                                        .With<RenderUtils::GlowComponent>(
                                            RenderUtils::GlowComponent(IM_COL32(102, 180, 255, 255), 22.0f, 0.86f)
                                                .SetShaderKey("glow.default")
                                                .SetMode(RenderUtils::GlowMode::GaussianBloom)
                                                .SetCoreStrength(0.30f)
                                                .SetClipToParent(false)
                                                .SetRenderMode(RenderUtils::GlowRenderMode::Shader)
                                                .ParamBindFloat("u_time", "time", 0.0f))
                                        .With<RenderUtils::ShaderComponent>(
                                            RenderUtils::ShaderComponent()
                                                .SetPixelSource(
                                                    RenderUtils::ShaderSourceSpec()
                                                        .SetMode(RenderUtils::ShaderSourceMode::EmbeddedCpp)
                                                        .SetSource("glow.default")
                                                        .SetTarget("ps_5_0")))
                                        .With<RenderUtils::TextComponent>(
                                            RenderUtils::TextComponent("Gaussian", IM_COL32(235, 245, 255, 255))
                                                .Align(RenderUtils::TextAlign::Center))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("GlowNeonCard")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(214, 70)).SetSize(ImVec2(170, 230)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(45, 52, 61, 255)).SetRounding(10.0f))
                                        .With<RenderUtils::GlowComponent>(
                                            RenderUtils::GlowComponent(IM_COL32(88, 245, 225, 255), 22.0f, 0.82f)
                                                .SetShaderKey("glow.default")
                                                .SetMode(RenderUtils::GlowMode::NeonTube)
                                                .SetCoreStrength(0.42f)
                                                .SetInnerGlow(true)
                                                .SetClipToParent(false)
                                                .SetRenderMode(RenderUtils::GlowRenderMode::Shader)
                                                .ParamBindFloat("u_time", "time", 0.0f)
                                                .ParamBindVec2("u_mouse", "mouse", ImVec2(0.0f, 0.0f)))
                                        .With<RenderUtils::ShaderComponent>(
                                            RenderUtils::ShaderComponent()
                                                .SetPixelSource(
                                                    RenderUtils::ShaderSourceSpec()
                                                        .SetMode(RenderUtils::ShaderSourceMode::EmbeddedCpp)
                                                        .SetSource("glow.default")
                                                        .SetTarget("ps_5_0")))
                                        .With<RenderUtils::TextComponent>(
                                            RenderUtils::TextComponent("Neon", IM_COL32(235, 245, 255, 255))
                                                .Align(RenderUtils::TextAlign::Center))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("GlowAmbientCard")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(408, 70)).SetSize(ImVec2(170, 230)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(47, 53, 63, 255)).SetRounding(10.0f))
                                        .With<RenderUtils::GlowComponent>(
                                            RenderUtils::GlowComponent(IM_COL32(198, 160, 255, 255), 27.0f, 0.78f)
                                                .SetShaderKey("glow.default")
                                                .SetMode(RenderUtils::GlowMode::AmbientSoft)
                                                .SetCoreStrength(0.26f)
                                                .SetClipToParent(false)
                                                .SetRenderMode(RenderUtils::GlowRenderMode::Shader)
                                                .ParamBindFloat("u_time", "time", 0.0f))
                                        .With<RenderUtils::ShaderComponent>(
                                            RenderUtils::ShaderComponent()
                                                .SetPixelSource(
                                                    RenderUtils::ShaderSourceSpec()
                                                        .SetMode(RenderUtils::ShaderSourceMode::EmbeddedCpp)
                                                        .SetSource("glow.default")
                                                        .SetTarget("ps_5_0")))
                                        .With<RenderUtils::TextComponent>(
                                            RenderUtils::TextComponent("Ambient", IM_COL32(235, 245, 255, 255))
                                                .Align(RenderUtils::TextAlign::Center))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("EffectsGlowQualityOptions")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 312)).SetSize(ImVec2(542, 34)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetContentPadding(4.0f, 4.0f))
                                        .With<RenderUtils::OptionsComponent>(RenderUtils::OptionsComponent({ "Performance", "Balanced", "Ultra" }, 2))
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("EffectsCustomAnimCard")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 396)).SetSize(ImVec2(1214, 180)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(39, 43, 52, 255)).SetRounding(8.0f).SetBorderColor(IM_COL32(71, 80, 96, 255)).SetBorderSize(1.0f))
                                    .With<RenderUtils::ShaderComponent>(
                                        RenderUtils::ShaderComponent()
                                            .SetPixelSource(
                                                RenderUtils::ShaderSourceSpec()
                                                    .SetMode(RenderUtils::ShaderSourceMode::EmbeddedCpp)
                                                    .SetSource("glow.default")
                                                    .SetTarget("ps_5_0"))
                                            .ConfigureParameters(
                                                RenderUtils::ShaderParamBuilder()
                                                    .BindFloat("u_time", "time", 0.0f)
                                                    .BindVec2("u_mouse", "mouse", ImVec2(0.0f, 0.0f))))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Custom + Animation: click panel to pulse glow", IM_COL32(220, 230, 245, 255)))
                                    .With<RenderUtils::CustomComponent>(
                                        RenderUtils::CustomComponent()
                                            .SetOnInput([](entt::registry& reg, entt::entity e, const RenderUtils::InputStateComponent& input) {
                                                if (!input.JustPressed || !input.IsHovered) {
                                                    return;
                                                }
                                                const bool enabled = reg.any_of<RenderUtils::GlowComponent>(e);
                                                if (!enabled) {
                                                    reg.emplace_or_replace<RenderUtils::GlowComponent>(
                                                        e,
                                                        RenderUtils::GlowComponent(IM_COL32(88, 245, 225, 255), 20.0f, 0.62f)
                                                            .SetShaderKey("glow.default")
                                                            .SetMode(RenderUtils::GlowMode::NeonTube)
                                                            .SetCoreStrength(0.44f)
                                                            .SetInnerGlow(true)
                                                            .SetClipToParent(false)
                                                            .SetSamples(14)
                                                            .ParamBindFloat("u_time", "time", 0.0f)
                                                            .ParamBindVec2("u_mouse", "mouse", ImVec2(0.0f, 0.0f)));

                                                    RenderUtils::AnimationBuilder(reg, e)
                                                        .Loop(true)
                                                        .Duration(1.6f)
                                                        .Ease(RenderUtils::EasingType::EaseInOutQuad)
                                                        .Custom([](float t, entt::registry& r, entt::entity ent) {
                                                            if (!r.valid(ent) || !r.any_of<RenderUtils::GlowComponent>(ent)) {
                                                                return;
                                                            }
                                                            auto& glow = r.get<RenderUtils::GlowComponent>(ent);
                                                            glow.SetIntensity(0.26f + 0.30f * std::fabs(std::sin(t * 6.28318f)));
                                                        })
                                                        .Start();
                                                } else {
                                                    if (reg.any_of<RenderUtils::AnimationComponent>(e)) {
                                                        reg.remove<RenderUtils::AnimationComponent>(e);
                                                    }
                                                    reg.remove<RenderUtils::GlowComponent>(e);
                                                }
                                            })
                                            .SetOnRender([](entt::registry& reg, entt::entity e, ImDrawList* dl, ImVec2 pMin, ImVec2 pMax, bool hovered, bool) {
                                                const bool enabled = reg.any_of<RenderUtils::GlowComponent>(e);
                                                const ImU32 stroke = enabled ? IM_COL32(94, 240, 230, 255) : IM_COL32(122, 132, 146, 255);
                                                dl->AddRect(ImVec2(pMin.x + 16, pMin.y + 50), ImVec2(pMax.x - 16, pMax.y - 16), stroke, 6.0f, 0, 1.5f);
                                                if (hovered) {
                                                    dl->AddText(ImVec2(pMin.x + 24, pMax.y - 34), IM_COL32(235, 244, 255, 235), enabled ? "Pulse active" : "Hover + click");
                                                }
                                            })
                                    )
                                )
                        )

                        // Media
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Panel>("TabMedia")
                                .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(0, 0)).SetSize(ImVec2(1250, 774)))
                                .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetVisible(false))
                                .IsTab((int)DemoTab::Media)
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("MediaStaticImage")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 18)).SetSize(ImVec2(390, 270)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Static image", IM_COL32(220, 230, 245, 255)))
                                    .With<Components::ImageLoader>(
                                        Components::ImageLoader()
                                            .AddUrl("https://upload.wikimedia.org/wikipedia/commons/3/3f/Fronalpstock_big.jpg"))
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("MultiImagePanel")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(430, 18)).SetSize(ImVec2(390, 270)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Multi-source image (click to cycle)", IM_COL32(220, 230, 245, 255)))
                                    .With<Components::ImageLoader>([](){
                                        Components::ImageLoader loader;
                                        loader.AddUrl("https://upload.wikimedia.org/wikipedia/commons/thumb/a/a9/Example.jpg/640px-Example.jpg");
                                        loader.AddUrl("https://upload.wikimedia.org/wikipedia/commons/9/9a/Gull_portrait_ca_usa.jpg");
                                        loader.AddPath("assets/local_preview.png");
                                        loader.SetActiveSource(0);
                                        return loader;
                                    }())
                                    .With<RenderUtils::CustomComponent>(RenderUtils::CustomComponent().SetOnInput([](entt::registry& reg, entt::entity e, const RenderUtils::InputStateComponent& input) {
                                        if (!input.JustPressed || !input.IsHovered || !reg.any_of<Components::ImageLoader>(e)) {
                                            return;
                                        }
                                        Components::ImageLoaderSystem::CycleSource(reg, e, +1);
                                    }))
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("MediaGifPanel")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(842, 18)).SetSize(ImVec2(390, 270)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Animated GIF", IM_COL32(220, 230, 245, 255)))
                                    .With<Components::ImageLoader>(
                                        Components::ImageLoader()
                                            .AddUrl("https://media.giphy.com/media/ICOgUNjpvO0PC/giphy.gif")
                                            .SetPlayback(true, true, false, 1.0f))
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("MediaPlaybackOptionsCard")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 310)).SetSize(ImVec2(1214, 150)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Options + Slider control GIF playback", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("MediaGifPlayOptions")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 56)).SetSize(ImVec2(360, 42)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetContentPadding(4.0f, 4.0f))
                                        .With<RenderUtils::OptionsComponent>(RenderUtils::OptionsComponent({ "Play", "Pause" }, 0, [](int index) {
                                            auto gif = RenderUtils::UIRenderer::FindEntityByName(g_registry, "MediaGifPanel");
                                            if (g_registry.valid(gif) && g_registry.any_of<Components::ImageLoader>(gif)) {
                                                g_registry.get<Components::ImageLoader>(gif).Paused = (index == 1);
                                            }
                                        }))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("MediaGifSpeed")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(420, 58)).SetSize(ImVec2(340, 38)))
                                        .With<RenderUtils::SliderComponent>(RenderUtils::SliderComponent(1.0f, 0.25f, 2.0f).SetSmoothing(true, 12.0f))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("MediaCycleModeOptions")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(790, 56)).SetSize(ImVec2(404, 42)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetContentPadding(4.0f, 4.0f))
                                        .With<RenderUtils::OptionsComponent>(RenderUtils::OptionsComponent({ "Loaded Only", "Cycle All", "Refetch" }, 0))
                                    )
                                )
                        )

                        // Interaction
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Panel>("TabInteraction")
                                .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(0, 0)).SetSize(ImVec2(1250, 774)))
                                .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetVisible(false))
                                .IsTab((int)DemoTab::Interaction)
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("InteractionCollisionStage")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 18)).SetSize(ImVec2(800, 340)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Collision + Draggable", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("CollisionBoxA")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(40, 116)).SetSize(ImVec2(180, 110)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(108, 170, 255, 210)).SetRounding(7.0f))
                                        .With<RenderUtils::DraggableComponent>(RenderUtils::DraggableComponent().SetMode(RenderUtils::DragMode::Free).SetConstraint(RenderUtils::DragConstraint::Parent))
                                        .With<RenderUtils::CollisionComponent>(RenderUtils::CollisionComponent(true))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Box A", IM_COL32(255, 255, 255, 255)))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("CollisionBoxB")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(300, 140)).SetSize(ImVec2(180, 110)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(255, 156, 110, 210)).SetRounding(7.0f))
                                        .With<RenderUtils::DraggableComponent>(RenderUtils::DraggableComponent().SetMode(RenderUtils::DragMode::Free).SetConstraint(RenderUtils::DragConstraint::Parent))
                                        .With<RenderUtils::CollisionComponent>(RenderUtils::CollisionComponent(true))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Box B", IM_COL32(255, 255, 255, 255)))
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("InteractionLockedStage")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(836, 18)).SetSize(ImVec2(396, 340)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Locked + DrawAbove", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("LockedObject")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(40, 116)).SetSize(ImVec2(140, 90)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(78, 95, 124, 255)).SetRounding(7.0f))
                                        .With<RenderUtils::DraggableComponent>(RenderUtils::DraggableComponent().SetMode(RenderUtils::DragMode::Free))
                                        .With<RenderUtils::LockedComponent>(RenderUtils::LockedComponent().SetLocked(true))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Locked", IM_COL32(236, 242, 255, 255)).Align(RenderUtils::TextAlign::Center))
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("InteractionLayerStage")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 376)).SetSize(ImVec2(610, 232)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("DrawAbove + Transparency", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("LayerBaseCard")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(60, 92)).SetSize(ImVec2(220, 110)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(95, 132, 206, 220)).SetRounding(8.0f))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Base", IM_COL32(255, 255, 255, 255)))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("LayerTopCard")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(190, 118)).SetSize(ImVec2(240, 106)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(232, 149, 105, 220)).SetRounding(8.0f))
                                        .With<RenderUtils::TransparencyComponent>(RenderUtils::TransparencyComponent(0.92f))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("DrawAbove target", IM_COL32(255, 255, 255, 255)))
                                        .DrawAbove("LayerBaseCard")
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("InteractionExpandStage")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(646, 376)).SetSize(ImVec2(586, 112)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f).SetBorderColor(IM_COL32(70, 78, 95, 255)).SetBorderSize(1.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("ExpandComponent (click to toggle size)", IM_COL32(220, 230, 245, 255)))
                                    .With<RenderUtils::ExpandComponent>(RenderUtils::ExpandComponent().SetExpanded(false).SetExpandedHeight(208.0f))
                                    .With<RenderUtils::CustomComponent>(RenderUtils::CustomComponent().SetOnInput([](entt::registry& reg, entt::entity e, const RenderUtils::InputStateComponent& input){
                                        if (!input.JustPressed || !input.IsHovered || !reg.all_of<RenderUtils::ExpandComponent, RenderUtils::TransformComponent>(e)) {
                                            return;
                                        }
                                        auto& exp = reg.get<RenderUtils::ExpandComponent>(e);
                                        auto& trans = reg.get<RenderUtils::TransformComponent>(e);
                                        exp.IsExpanded = !exp.IsExpanded;
                                        trans.Size.y = exp.IsExpanded ? exp.ExpandedHeight : 112.0f;
                                    }))
                                )
                        )

                        // Systems
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Panel>("TabSystems")
                                .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(0, 0)).SetSize(ImVec2(1250, 774)))
                                .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetVisible(false))
                                .IsTab((int)DemoTab::Systems)
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("SystemsControlCard")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 18)).SetSize(ImVec2(610, 280)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Startup controls (integrated)", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysModeOptions")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 52)).SetSize(ImVec2(570, 38)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetContentPadding(4.0f, 4.0f))
                                        .With<RenderUtils::OptionsComponent>(RenderUtils::OptionsComponent({ "Block On Required Assets", "Immediate UI" }, 0))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysPriorityOptions")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 100)).SetSize(ImVec2(570, 38)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetContentPadding(4.0f, 4.0f))
                                        .With<RenderUtils::OptionsComponent>(RenderUtils::OptionsComponent({ "Images First", "Systems First", "Balanced" }, 0))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysTimeoutSlider")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 152)).SetSize(ImVec2(570, 34)))
                                        .With<RenderUtils::SliderComponent>(RenderUtils::SliderComponent(15.0f, 1.0f, 120.0f).SetSmoothing(true, 12.0f))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Button>("SysRestartButton")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 200)).SetSize(ImVec2(278, 40)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(64, 83, 118, 255)).SetRounding(5.0f))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Restart Startup Gate", IM_COL32(255, 255, 255, 255)).Align(RenderUtils::TextAlign::Center))
                                        .With<RenderUtils::CustomComponent>(RenderUtils::CustomComponent().SetOnInput([](entt::registry&, entt::entity, const RenderUtils::InputStateComponent& input) {
                                            if (input.JustPressed && input.IsHovered) {
                                                g_RequestStartupRestart = true;
                                            }
                                        }))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Button>("SysDebugToggleButton")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(312, 200)).SetSize(ImVec2(278, 40)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(78, 70, 122, 255)).SetRounding(5.0f))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Debug Mode: Off", IM_COL32(255, 255, 255, 255)).Align(RenderUtils::TextAlign::Center))
                                        .With<RenderUtils::CustomComponent>(RenderUtils::CustomComponent().SetOnInput([](entt::registry&, entt::entity, const RenderUtils::InputStateComponent& input) {
                                            if (input.JustPressed && input.IsHovered) {
                                                RenderUtils::UIRenderer::DebugMode = !RenderUtils::UIRenderer::DebugMode;
                                            }
                                        }))
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("SystemsInfoCard")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(648, 18)).SetSize(ImVec2(584, 280)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Runtime diagnostics", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysStatusText")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 48)).SetSize(ImVec2(548, 56)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(0, 0, 0, 0)).SetContentPadding(0.0f, 0.0f))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Status pending...", IM_COL32(205, 220, 245, 255)))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysLoaderStatsText")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 112)).SetSize(ImVec2(548, 96)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(0, 0, 0, 0)).SetContentPadding(0.0f, 0.0f))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Loader stats pending...", IM_COL32(195, 212, 235, 255)))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysSummaryText")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 214)).SetSize(ImVec2(548, 54)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(0, 0, 0, 0)).SetContentPadding(0.0f, 0.0f))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Summary pending...", IM_COL32(245, 220, 170, 255)))
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("SystemsProgressCard")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 316)).SetSize(ImVec2(1214, 292)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Startup progress", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysProgressBarTrack")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 52)).SetSize(ImVec2(1178, 28)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(30, 34, 42, 255)).SetRounding(4.0f).SetBorderColor(IM_COL32(72, 82, 100, 255)).SetBorderSize(1.0f))
                                        .With<RenderUtils::ClipComponent>(RenderUtils::ClipComponent().SetClipChildren(true))
                                        .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                            .Create<RenderUtils::ContainerType::Panel>("SysProgressBarFill")
                                            .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(2, 2)).SetSize(ImVec2(4, 24)))
                                            .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(240, 194, 84, 255)).SetRounding(3.0f))
                                        )
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysProgressText")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 88)).SetSize(ImVec2(1178, 188)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(0, 0, 0, 0)).SetContentPadding(0.0f, 0.0f))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Progress pending...", IM_COL32(212, 224, 245, 255)))
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("SystemsShaderCard")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 622)).SetSize(ImVec2(1214, 130)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(40, 44, 53, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Shader runtime controls", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysShaderPolicyOptions")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 38)).SetSize(ImVec2(280, 34)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetContentPadding(4.0f, 4.0f))
                                        .With<RenderUtils::OptionsComponent>(RenderUtils::OptionsComponent({ "OnDemand+Cache", "Startup", "Manual Apply" }, 0))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysShaderBackendOptions")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(312, 38)).SetSize(ImVec2(280, 34)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetContentPadding(4.0f, 4.0f))
                                        .With<RenderUtils::OptionsComponent>(RenderUtils::OptionsComponent({ "AutoPreferDXC", "D3DCompileOnly", "DXCOnly" }, 0))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysGlowRenderModeOptions")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(606, 38)).SetSize(ImVec2(180, 34)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetContentPadding(4.0f, 4.0f))
                                        .With<RenderUtils::OptionsComponent>(RenderUtils::OptionsComponent({ "Glow Shader", "Glow CPU" }, 0))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysShadowRenderModeOptions")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(798, 38)).SetSize(ImVec2(180, 34)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetContentPadding(4.0f, 4.0f))
                                        .With<RenderUtils::OptionsComponent>(RenderUtils::OptionsComponent({ "Shadow CPU", "Shadow Shader" }, 0))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Button>("SysShaderApplyButton")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(990, 38)).SetSize(ImVec2(206, 34)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(76, 96, 136, 255)).SetRounding(5.0f))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Apply/Recompile Shaders", IM_COL32(255, 255, 255, 255)).Align(RenderUtils::TextAlign::Center))
                                        .With<RenderUtils::CustomComponent>(RenderUtils::CustomComponent().SetOnInput([](entt::registry&, entt::entity, const RenderUtils::InputStateComponent& input) {
                                            if (input.JustPressed && input.IsHovered) {
                                                g_RequestShaderRecompile = true;
                                            }
                                        }))
                                    )
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("SysShaderStatusText")
                                        .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 80)).SetSize(ImVec2(1178, 40)))
                                        .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(0, 0, 0, 0)).SetContentPadding(0.0f, 0.0f))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Shader status pending...", IM_COL32(205, 220, 245, 255)))
                                    )
                                )
                        )
                )
            .End();
        // ------------------------------------------------

        ApplyRuntimeFeatureRestrictions(g_registry);
    }

    void ShowcaseRuntime::RenderFrame(const DX12Init::FramePacket& frame) {
            ImGuiIO& io = frame.IO ? *frame.IO : ImGui::GetIO();

            auto mainEnt = RenderUtils::UIRenderer::FindEntityByName(g_registry, "MainContainer");
            if (g_registry.valid(mainEnt)) {
                g_registry.patch<RenderUtils::TransformComponent>(mainEnt, [&io](auto& transform) {
                    transform.Size = io.DisplaySize;
                });
            }

            const float deltaTime = frame.DeltaTime;
            RenderUtils::StartupRuntime::Update(g_registry, deltaTime, startupConfig, startupState);

            auto findEntity = [](const char* name) -> entt::entity {
                return RenderUtils::UIRenderer::FindEntityByName(g_registry, name);
            };

            auto fontStateLabel = [](RenderUtils::FontFaceRuntimeState state) -> const char* {
                switch (state) {
                case RenderUtils::FontFaceRuntimeState::Missing:
                    return "missing";
                case RenderUtils::FontFaceRuntimeState::Loading:
                    return "loading";
                case RenderUtils::FontFaceRuntimeState::Ready:
                    return "ready";
                case RenderUtils::FontFaceRuntimeState::Failed:
                    return "failed";
                default:
                    return "unknown";
                }
            };

            auto computeFontOptionIndex = [](const std::vector<std::string>& options, const std::string& key) -> int {
                if (key.empty()) {
                    return 0;
                }
                for (size_t i = 0; i < options.size(); ++i) {
                    if (options[i] == key) {
                        return static_cast<int>(i);
                    }
                }
                return 0;
            };

            entt::entity showcaseEnt = findEntity("ModMenuShowcase");
            entt::entity fontSwitchOptionsEnt = findEntity("OverviewFontSwitchOptions");
            entt::entity fontSwitchStatusEnt = findEntity("OverviewFontSwitchStatus");
            std::string runtimeActiveFaceLabel = "<ImGui Default>";
            RenderUtils::FontFaceRuntimeState runtimeActiveFaceState = RenderUtils::FontFaceRuntimeState::Ready;
            if (g_registry.valid(showcaseEnt) && g_registry.any_of<RenderUtils::FontsComponent>(showcaseEnt)) {
                auto& showcaseFonts = g_registry.get<RenderUtils::FontsComponent>(showcaseEnt);
                if (!showcaseFonts.DefaultFaceKey.empty() && !showcaseFonts.HasFace(showcaseFonts.DefaultFaceKey)) {
                    showcaseFonts.DefaultFaceKey.clear();
                }

                const std::string bodyLocalFaceKey = "ui.body";
                const std::string bodyWebFaceKey = "ui.body.web";
                auto queryFaceState = [](const std::vector<RenderUtils::FontFaceRuntimeInfo>& infos, const std::string& key) {
                    if (key.empty()) {
                        return RenderUtils::FontFaceRuntimeState::Ready;
                    }
                    for (const auto& info : infos) {
                        if (info.Key == key) {
                            return info.State;
                        }
                    }
                    return RenderUtils::FontFaceRuntimeState::Missing;
                };

                std::vector<std::string> desiredOptions;
                desiredOptions.emplace_back("<ImGui Default>");
                const std::vector<std::string> faceKeys = showcaseFonts.GetFaceKeys();
                desiredOptions.insert(desiredOptions.end(), faceKeys.begin(), faceKeys.end());

                if (g_registry.valid(fontSwitchOptionsEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(fontSwitchOptionsEnt)) {
                    auto& options = g_registry.get<RenderUtils::OptionsComponent>(fontSwitchOptionsEnt);
                    if (options.Options != desiredOptions) {
                        options.Options = desiredOptions;
                    }
                    if (options.Options.empty()) {
                        options.Options = desiredOptions;
                    }
                    if (options.SelectedIndex < 0) {
                        options.SelectedIndex = 0;
                    }
                    if (options.SelectedIndex >= static_cast<int>(options.Options.size())) {
                        options.SelectedIndex = computeFontOptionIndex(options.Options, showcaseFonts.DefaultFaceKey);
                    }

                    std::string requestedKey;
                    if (options.SelectedIndex > 0 &&
                        options.SelectedIndex < static_cast<int>(options.Options.size())) {
                        requestedKey = options.Options[static_cast<size_t>(options.SelectedIndex)];
                    }
                    if (requestedKey != showcaseFonts.DefaultFaceKey) {
                        showcaseFonts.SetDefaultFaceSafe(requestedKey);
                    }
                }

                std::vector<RenderUtils::FontFaceRuntimeInfo> infos =
                    RenderUtils::FontSystem::QueryEntityFontFaces(g_registry, showcaseEnt, false);
                if (g_RuntimeOptions.PreferRemoteBodyFont) {
                    const bool managesBodyDefault = showcaseFonts.DefaultFaceKey.empty() ||
                        showcaseFonts.DefaultFaceKey == bodyLocalFaceKey ||
                        showcaseFonts.DefaultFaceKey == bodyWebFaceKey;
                    if (managesBodyDefault) {
                        const RenderUtils::FontFaceRuntimeState bodyLocalState = queryFaceState(infos, bodyLocalFaceKey);
                        const RenderUtils::FontFaceRuntimeState bodyWebState = queryFaceState(infos, bodyWebFaceKey);
                        if (bodyWebState == RenderUtils::FontFaceRuntimeState::Ready && showcaseFonts.HasFace(bodyWebFaceKey)) {
                            if (showcaseFonts.DefaultFaceKey != bodyWebFaceKey) {
                                showcaseFonts.SetDefaultFaceSafe(bodyWebFaceKey);
                            }
                        } else if (bodyLocalState == RenderUtils::FontFaceRuntimeState::Ready &&
                                   showcaseFonts.HasFace(bodyLocalFaceKey) &&
                                   showcaseFonts.DefaultFaceKey.empty()) {
                            showcaseFonts.SetDefaultFaceSafe(bodyLocalFaceKey);
                        } else if (showcaseFonts.DefaultFaceKey == bodyLocalFaceKey &&
                                   bodyLocalState != RenderUtils::FontFaceRuntimeState::Ready) {
                            showcaseFonts.DefaultFaceKey.clear();
                        }
                    }
                }

                infos = RenderUtils::FontSystem::QueryEntityFontFaces(g_registry, showcaseEnt, false);
                if (g_registry.valid(fontSwitchOptionsEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(fontSwitchOptionsEnt)) {
                    auto& options = g_registry.get<RenderUtils::OptionsComponent>(fontSwitchOptionsEnt);
                    const int syncedIndex = computeFontOptionIndex(options.Options, showcaseFonts.DefaultFaceKey);
                    if (options.SelectedIndex != syncedIndex) {
                        options.SelectedIndex = syncedIndex;
                    }
                }

                const RenderUtils::FontFaceRuntimeState activeState = queryFaceState(infos, showcaseFonts.DefaultFaceKey);
                runtimeActiveFaceLabel = showcaseFonts.DefaultFaceKey.empty() ? "<ImGui Default>" : showcaseFonts.DefaultFaceKey;
                runtimeActiveFaceState = activeState;

                if (g_registry.valid(fontSwitchStatusEnt) && g_registry.any_of<RenderUtils::TextComponent>(fontSwitchStatusEnt)) {
                    auto& statusText = g_registry.get<RenderUtils::TextComponent>(fontSwitchStatusEnt);
                    const std::string nextText = "Active: " + runtimeActiveFaceLabel + " (" + fontStateLabel(activeState) + ")";
                    if (statusText.RawText != nextText) {
                        statusText.RawText = nextText;
                    }

                    const ImU32 nextColor = activeState == RenderUtils::FontFaceRuntimeState::Ready
                        ? IM_COL32(182, 228, 196, 255)
                        : (activeState == RenderUtils::FontFaceRuntimeState::Loading
                            ? IM_COL32(238, 220, 170, 255)
                            : (activeState == RenderUtils::FontFaceRuntimeState::Failed
                                ? IM_COL32(255, 178, 178, 255)
                                : IM_COL32(190, 208, 235, 255)));
                    if (statusText.Color != nextColor) {
                        statusText.Color = nextColor;
                    }
                }
            }

            // Systems tab -> runtime startup config sync
            entt::entity modeEnt = findEntity("SysModeOptions");
            if (g_registry.valid(modeEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(modeEnt)) {
                auto& options = g_registry.get<RenderUtils::OptionsComponent>(modeEnt);
                int selected = options.SelectedIndex;
                if (selected < 0) selected = 0;
                if (selected > 1) selected = 1;
                if (selected != options.SelectedIndex) {
                    options.SelectedIndex = selected;
                }

                const RenderUtils::StartupRuntime::Mode requestedMode = (selected == 1)
                    ? RenderUtils::StartupRuntime::Mode::ImmediateUI
                    : RenderUtils::StartupRuntime::Mode::BlockOnRequiredImages;
                if (requestedMode != startupConfig.ModeValue) {
                    startupConfig.ModeValue = requestedMode;
                    if (startupConfig.ModeValue == RenderUtils::StartupRuntime::Mode::ImmediateUI) {
                        startupState.Completed = true;
                        startupState.TimedOut = false;
                        startupState.SummaryLine = "Startup mode switched to Immediate UI.";
                    } else {
                        RenderUtils::StartupRuntime::Reset(startupState);
                    }
                }
            }

            entt::entity priorityEnt = findEntity("SysPriorityOptions");
            if (g_registry.valid(priorityEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(priorityEnt)) {
                auto& options = g_registry.get<RenderUtils::OptionsComponent>(priorityEnt);
                int selected = options.SelectedIndex;
                if (selected < 0) selected = 0;
                if (selected > 2) selected = 2;
                if (selected != options.SelectedIndex) {
                    options.SelectedIndex = selected;
                }
                startupConfig.PriorityValue = static_cast<RenderUtils::StartupRuntime::Priority>(selected);
            }

            entt::entity timeoutEnt = findEntity("SysTimeoutSlider");
            if (g_registry.valid(timeoutEnt) && g_registry.any_of<RenderUtils::SliderComponent>(timeoutEnt)) {
                auto& slider = g_registry.get<RenderUtils::SliderComponent>(timeoutEnt);
                if (slider.Value < slider.Min) slider.Value = slider.Min;
                if (slider.Value > slider.Max) slider.Value = slider.Max;
                startupConfig.TimeoutSeconds = slider.Value;
            }

            // Media controls -> GIF speed sync
            entt::entity speedEnt = findEntity("MediaGifSpeed");
            entt::entity gifEnt = findEntity("MediaGifPanel");
            if (g_registry.valid(speedEnt) && g_registry.any_of<RenderUtils::SliderComponent>(speedEnt) &&
                g_registry.valid(gifEnt) && g_registry.any_of<Components::ImageLoader>(gifEnt)) {
                const auto& speedSlider = g_registry.get<RenderUtils::SliderComponent>(speedEnt);
                auto& gifLoader = g_registry.get<Components::ImageLoader>(gifEnt);
                float speed = speedSlider.Value;
                if (speed < 0.25f) speed = 0.25f;
                if (speed > 2.0f) speed = 2.0f;
                gifLoader.PlaybackSpeed = speed;
            }

            entt::entity cycleModeEnt = findEntity("MediaCycleModeOptions");
            entt::entity multiImageEnt = findEntity("MultiImagePanel");
            if (g_registry.valid(cycleModeEnt) &&
                g_registry.any_of<RenderUtils::OptionsComponent>(cycleModeEnt) &&
                g_registry.valid(multiImageEnt) &&
                g_registry.any_of<Components::ImageLoader>(multiImageEnt)) {
                auto& options = g_registry.get<RenderUtils::OptionsComponent>(cycleModeEnt);
                int selected = options.SelectedIndex;
                if (selected < 0) selected = 0;
                if (selected > 2) selected = 2;
                if (selected != options.SelectedIndex) {
                    options.SelectedIndex = selected;
                }

                auto& loader = g_registry.get<Components::ImageLoader>(multiImageEnt);
                loader.CycleMode = selected == 1
                    ? Components::ImageLoader::SourceCycleMode::CycleAllSources
                    : (selected == 2
                        ? Components::ImageLoader::SourceCycleMode::RefetchOnClick
                        : Components::ImageLoader::SourceCycleMode::CycleLoadedOnly);
            }

            entt::entity glowQualityEnt = findEntity("EffectsGlowQualityOptions");
            if (g_registry.valid(glowQualityEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(glowQualityEnt)) {
                auto& options = g_registry.get<RenderUtils::OptionsComponent>(glowQualityEnt);
                int selected = options.SelectedIndex;
                if (selected < 0) selected = 0;
                if (selected > 2) selected = 2;
                if (selected != options.SelectedIndex) {
                    options.SelectedIndex = selected;
                }

                const RenderUtils::GlowQualityMode quality = selected == 0
                    ? RenderUtils::GlowQualityMode::Performance
                    : (selected == 1 ? RenderUtils::GlowQualityMode::Balanced : RenderUtils::GlowQualityMode::Ultra);
                const char* glowTargets[] = {
                    "GlowGaussianCard",
                    "GlowNeonCard",
                    "GlowAmbientCard",
                    "EffectsCustomAnimCard"
                };
                for (const char* name : glowTargets) {
                    entt::entity glowEnt = findEntity(name);
                    if (!g_registry.valid(glowEnt) || !g_registry.any_of<RenderUtils::GlowComponent>(glowEnt)) {
                        continue;
                    }
                    auto& glow = g_registry.get<RenderUtils::GlowComponent>(glowEnt);
                    if (glow.QualityMode != quality) {
                        glow.QualityMode = quality;
                        glow.MarkCacheDirty();
                    }
                }
            }

            const bool imageLoadingEnabled = g_RuntimeOptions.EnableImageLoading;
            const bool shaderLoadingEnabled = g_RuntimeOptions.EnableShaderLoading;

            RenderUtils::ShaderCompilePolicy shaderPolicy = RenderUtils::ShaderCompilePolicy::OnDemandCache;
            RenderUtils::ShaderBackendMode shaderBackend = RenderUtils::ShaderBackendMode::AutoPreferDXC;
            entt::entity shaderPolicyEnt = findEntity("SysShaderPolicyOptions");
            entt::entity shaderBackendEnt = findEntity("SysShaderBackendOptions");
            entt::entity glowModeEnt = findEntity("SysGlowRenderModeOptions");
            entt::entity shadowModeEnt = findEntity("SysShadowRenderModeOptions");

            if (shaderLoadingEnabled) {
                if (g_registry.valid(shaderPolicyEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(shaderPolicyEnt)) {
                    auto& options = g_registry.get<RenderUtils::OptionsComponent>(shaderPolicyEnt);
                    int selected = options.SelectedIndex;
                    if (selected < 0) selected = 0;
                    if (selected > 2) selected = 2;
                    if (selected != options.SelectedIndex) {
                        options.SelectedIndex = selected;
                    }
                    shaderPolicy = selected == 1
                        ? RenderUtils::ShaderCompilePolicy::StartupPrecompile
                        : (selected == 2
                            ? RenderUtils::ShaderCompilePolicy::ManualApply
                            : RenderUtils::ShaderCompilePolicy::OnDemandCache);
                }

                if (g_registry.valid(shaderBackendEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(shaderBackendEnt)) {
                    auto& options = g_registry.get<RenderUtils::OptionsComponent>(shaderBackendEnt);
                    int selected = options.SelectedIndex;
                    if (selected < 0) selected = 0;
                    if (selected > 2) selected = 2;
                    if (selected != options.SelectedIndex) {
                        options.SelectedIndex = selected;
                    }
                    shaderBackend = static_cast<RenderUtils::ShaderBackendMode>(selected);
                }
            } else {
                if (g_registry.valid(glowModeEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(glowModeEnt)) {
                    g_registry.get<RenderUtils::OptionsComponent>(glowModeEnt).SelectedIndex = 1;
                }
                if (g_registry.valid(shadowModeEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(shadowModeEnt)) {
                    g_registry.get<RenderUtils::OptionsComponent>(shadowModeEnt).SelectedIndex = 0;
                }
            }

            RenderUtils::GlowRenderMode glowRenderMode = shaderLoadingEnabled
                ? RenderUtils::GlowRenderMode::Shader
                : RenderUtils::GlowRenderMode::Cpu;
            if (shaderLoadingEnabled && g_registry.valid(glowModeEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(glowModeEnt)) {
                auto& options = g_registry.get<RenderUtils::OptionsComponent>(glowModeEnt);
                int selected = options.SelectedIndex;
                if (selected < 0) selected = 0;
                if (selected > 1) selected = 1;
                if (selected != options.SelectedIndex) {
                    options.SelectedIndex = selected;
                }
                glowRenderMode = selected == 1 ? RenderUtils::GlowRenderMode::Cpu : RenderUtils::GlowRenderMode::Shader;
            }

            RenderUtils::ShadowRenderMode shadowRenderMode = RenderUtils::ShadowRenderMode::Cpu;
            if (shaderLoadingEnabled && g_registry.valid(shadowModeEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(shadowModeEnt)) {
                auto& options = g_registry.get<RenderUtils::OptionsComponent>(shadowModeEnt);
                int selected = options.SelectedIndex;
                if (selected < 0) selected = 0;
                if (selected > 1) selected = 1;
                if (selected != options.SelectedIndex) {
                    options.SelectedIndex = selected;
                }
                shadowRenderMode = selected == 1 ? RenderUtils::ShadowRenderMode::Shader : RenderUtils::ShadowRenderMode::Cpu;
            }

            if (shaderLoadingEnabled) {
                auto shaderView = g_registry.view<RenderUtils::ShaderComponent>();
                for (auto entity : shaderView) {
                    auto& shader = shaderView.get<RenderUtils::ShaderComponent>(entity);
                    if (shader.CompilePolicy != shaderPolicy) {
                        shader.CompilePolicy = shaderPolicy;
                    }
                    if (shader.Backend != shaderBackend) {
                        shader.Backend = shaderBackend;
                        shader.Dirty = true;
                    }
                }
            } else {
                auto shaderView = g_registry.view<RenderUtils::ShaderComponent>();
                std::vector<entt::entity> shaderEntities;
                for (auto entity : shaderView) {
                    shaderEntities.push_back(entity);
                }
                for (auto entity : shaderEntities) {
                    if (g_registry.valid(entity) && g_registry.any_of<RenderUtils::ShaderComponent>(entity)) {
                        g_registry.remove<RenderUtils::ShaderComponent>(entity);
                    }
                }
            }

            const char* glowRenderTargets[] = {
                "GlowGaussianCard",
                "GlowNeonCard",
                "GlowAmbientCard",
                "EffectsCustomAnimCard"
            };
            for (const char* name : glowRenderTargets) {
                entt::entity glowEnt = findEntity(name);
                if (!g_registry.valid(glowEnt) || !g_registry.any_of<RenderUtils::GlowComponent>(glowEnt)) {
                    continue;
                }
                auto& glow = g_registry.get<RenderUtils::GlowComponent>(glowEnt);
                glow.RenderMode = glowRenderMode;
            }

            entt::entity shadowEntity = findEntity("ShadowCardObject");
            if (g_registry.valid(shadowEntity) && g_registry.any_of<RenderUtils::ShadowComponent>(shadowEntity)) {
                auto& shadow = g_registry.get<RenderUtils::ShadowComponent>(shadowEntity);
                shadow.RenderMode = shadowRenderMode;
            }

            if (g_RequestStartupRestart) {
                if (imageLoadingEnabled) {
                    Components::ImageLoaderSystem::RequestRestart(g_registry, false);
                }
                RenderUtils::StartupRuntime::Reset(startupState);
                g_RequestStartupRestart = false;
            }

            if (g_RequestShaderRecompile) {
                if (shaderLoadingEnabled) {
                    auto shaders = g_registry.view<RenderUtils::ShaderComponent>();
                    for (auto entity : shaders) {
                        auto& shader = shaders.get<RenderUtils::ShaderComponent>(entity);
                        shader.CompileRequested = true;
                        shader.Dirty = true;
                    }
                }
                g_RequestShaderRecompile = false;
            }

            const Components::ImageLoaderDebugStats loaderStats = Components::ImageLoaderSystem::QueryDebugStats(g_registry);
            const Components::ImageLoaderProgress imageProgress = Components::ImageLoaderSystem::QueryProgress(g_registry);
            const RenderUtils::FontLoadProgress fontProgress = RenderUtils::FontSystem::QueryProgress(g_registry);

            auto setPanelText = [](const char* name, const std::string& value, ImU32 color) {
                entt::entity entity = RenderUtils::UIRenderer::FindEntityByName(g_registry, name);
                if (g_registry.valid(entity) && g_registry.any_of<RenderUtils::TextComponent>(entity)) {
                    auto& text = g_registry.get<RenderUtils::TextComponent>(entity);
                    text.RawText = value;
                    text.Color = color;
                }
            };

            entt::entity transparencyEnt = findEntity("OverviewTransparency");
            if (g_registry.valid(transparencyEnt) &&
                g_registry.any_of<RenderUtils::TransparencyComponent>(transparencyEnt) &&
                g_registry.any_of<RenderUtils::TextComponent>(transparencyEnt)) {
                float alpha = g_registry.get<RenderUtils::TransparencyComponent>(transparencyEnt).Alpha;
                if (alpha < 0.0f) alpha = 0.0f;
                if (alpha > 1.0f) alpha = 1.0f;
                auto& text = g_registry.get<RenderUtils::TextComponent>(transparencyEnt);
                char alphaBuf[96] = {};
                std::snprintf(alphaBuf, sizeof(alphaBuf), "Transparency alpha: %.2f", alpha);
                const std::string nextText(alphaBuf);
                if (text.RawText != nextText) {
                    text.RawText = nextText;
                }
                const ImU32 nextColor = IM_COL32(12, 20, 30, 255);
                if (text.Color != nextColor) {
                    text.Color = nextColor;
                }
            }

            const char* modeLabel = startupConfig.ModeValue == RenderUtils::StartupRuntime::Mode::ImmediateUI
                ? "Immediate UI"
                : "Block On Required Assets";
            const char* priorityLabel = startupConfig.PriorityValue == RenderUtils::StartupRuntime::Priority::ImagesFirst
                ? "Images First"
                : (startupConfig.PriorityValue == RenderUtils::StartupRuntime::Priority::SystemsFirst ? "Systems First" : "Balanced");

            const int fpsInt = static_cast<int>(io.Framerate + 0.5f);
            const std::string statusText =
                "Gate: " + std::string(startupState.Completed ? "Open" : "Active") + "\n" +
                "Mode: " + modeLabel + "  Priority: " + priorityLabel + "\n" +
                "Timeout: " + std::to_string(static_cast<int>(startupConfig.TimeoutSeconds)) + " sec";
            setPanelText("SysStatusText", statusText, IM_COL32(205, 220, 245, 255));

            const std::string loaderText =
                "Entities: " + std::to_string(static_cast<int>(g_registry.storage<entt::entity>().size())) +
                "  FPS: " + std::to_string(fpsInt) + "\n" +
                "Descriptors Used/Free: " + std::to_string(loaderStats.descriptorUsed) + " / " + std::to_string(loaderStats.descriptorFree) + "\n" +
                "Deferred (res/srv): " + std::to_string(loaderStats.deferredResourceReleases) + " / " + std::to_string(loaderStats.deferredDescriptorRecycles) + "\n" +
                "Pending Fetches: " + std::to_string(loaderStats.pendingFetches) +
                "  Uploads: " + std::to_string(loaderStats.uploadedFramesLastTick) + " frames\n" +
                "Font: " + runtimeActiveFaceLabel + " (" + fontStateLabel(runtimeActiveFaceState) + ")\n" +
                "Image req R/L/F: " +
                std::to_string(imageProgress.readyRequired) + "/" +
                std::to_string(imageProgress.loadingRequired) + "/" +
                std::to_string(imageProgress.failedRequired) +
                "  Font req R/L/F: " +
                std::to_string(fontProgress.readyRequired) + "/" +
                std::to_string(fontProgress.loadingRequired) + "/" +
                std::to_string(fontProgress.failedRequired);
            setPanelText("SysLoaderStatsText", loaderText, IM_COL32(195, 212, 235, 255));

            int shaderTotal = 0;
            int shaderCompiled = 0;
            int shaderFailed = 0;
            int shaderManualPending = 0;
            auto shaderStatusView = g_registry.view<RenderUtils::ShaderComponent>();
            for (auto entity : shaderStatusView) {
                (void)entity;
                const auto& shader = shaderStatusView.get<RenderUtils::ShaderComponent>(entity);
                ++shaderTotal;
                if (shader.IsCompiled) {
                    ++shaderCompiled;
                }
                if (!shader.LastError.empty()) {
                    ++shaderFailed;
                }
                if (shader.CompilePolicy == RenderUtils::ShaderCompilePolicy::ManualApply && !shader.CompileRequested) {
                    ++shaderManualPending;
                }
            }
            const char* shaderPolicyLabel = shaderPolicy == RenderUtils::ShaderCompilePolicy::StartupPrecompile
                ? "Startup"
                : (shaderPolicy == RenderUtils::ShaderCompilePolicy::ManualApply ? "Manual Apply" : "OnDemand+Cache");
            const char* shaderBackendLabel = shaderBackend == RenderUtils::ShaderBackendMode::D3DCompileOnly
                ? "D3DCompile"
                : (shaderBackend == RenderUtils::ShaderBackendMode::DXCOnly ? "DXC" : "AutoPreferDXC");
            const char* glowModeLabel = glowRenderMode == RenderUtils::GlowRenderMode::Shader ? "Shader" : "CPU";
            const char* shadowModeLabel = shadowRenderMode == RenderUtils::ShadowRenderMode::Shader ? "Shader" : "CPU";
            const std::string shaderStatusText =
                "Policy: " + std::string(shaderPolicyLabel) + "  Backend: " + shaderBackendLabel +
                "  Glow: " + glowModeLabel + "  Shadow: " + shadowModeLabel + "\n" +
                "Shaders: " + std::to_string(shaderCompiled) + "/" + std::to_string(shaderTotal) +
                " compiled, " + std::to_string(shaderFailed) + " with errors, " +
                std::to_string(shaderManualPending) + " manual pending\n" +
                "Callback queued/fail: " +
                std::to_string(RenderUtils::ShaderSystem::QueryImGuiCallbackQueuedCount()) + " / " +
                std::to_string(RenderUtils::ShaderSystem::QueryImGuiCallbackRuntimeFailureCount());
            setPanelText("SysShaderStatusText", shaderStatusText, IM_COL32(205, 220, 245, 255));

            std::string summary = startupState.SummaryLine.empty()
                ? "Summary: waiting for startup result."
                : startupState.SummaryLine;
            setPanelText(
                "SysSummaryText",
                summary,
                startupState.TimedOut ? IM_COL32(255, 180, 180, 255) : IM_COL32(245, 220, 170, 255));

            std::string progressText =
                "Required: " + std::to_string(startupState.Progress.totalRequired) +
                "  Ready: " + std::to_string(startupState.Progress.readyRequired) +
                "  Loading: " + std::to_string(startupState.Progress.loadingRequired) +
                "  Failed: " + std::to_string(startupState.Progress.failedRequired) + "\n" +
                "Elapsed: " + std::to_string(static_cast<int>(startupState.ElapsedSec)) + " sec";
            if (!startupState.Progress.currentLabel.empty()) {
                progressText += "\nCurrent: " + startupState.Progress.currentLabel;
            }
            setPanelText("SysProgressText", progressText, IM_COL32(212, 224, 245, 255));

            entt::entity progressFillEnt = findEntity("SysProgressBarFill");
            if (g_registry.valid(progressFillEnt) &&
                g_registry.any_of<RenderUtils::TransformComponent>(progressFillEnt) &&
                g_registry.any_of<RenderUtils::StyleComponent>(progressFillEnt)) {
                auto& transform = g_registry.get<RenderUtils::TransformComponent>(progressFillEnt);
                auto& style = g_registry.get<RenderUtils::StyleComponent>(progressFillEnt);
                float percent = startupState.Progress.percent;
                if (!std::isfinite(percent)) percent = 0.0f;
                if (percent < 0.0f) percent = 0.0f;
                if (percent > 1.0f) percent = 1.0f;
                transform.Size.x = 1174.0f * percent;
                if (transform.Size.x < 2.0f) {
                    transform.Size.x = 2.0f;
                }
                style.BackgroundColor = startupState.Completed
                    ? IM_COL32(120, 210, 140, 255)
                    : IM_COL32(240, 194, 84, 255);
            }

            entt::entity debugToggleEnt = findEntity("SysDebugToggleButton");
            if (g_registry.valid(debugToggleEnt) && g_registry.any_of<RenderUtils::TextComponent>(debugToggleEnt)) {
                auto& text = g_registry.get<RenderUtils::TextComponent>(debugToggleEnt);
                text.RawText = RenderUtils::UIRenderer::DebugMode ? "Debug Mode: On" : "Debug Mode: Off";
            }

            // Render ECS UI
            RenderUtils::UIRenderer::Render(g_registry);

            // Render inspector only when enabled.
            if (RenderUtils::UIRenderer::DebugMode) {
                RenderUtils::UIRenderer::RenderInspector(g_registry);
            }

            if (startupConfig.ModeValue == RenderUtils::StartupRuntime::Mode::BlockOnRequiredImages && !startupState.Completed) {
                const ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
                ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                ImGui::SetNextWindowSize(ImVec2(500, 170), ImGuiCond_Always);
                ImGui::Begin("Startup Loading Overlay", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
                ImGui::Text("Loading required assets...");
                ImGui::ProgressBar(startupState.Progress.percent, ImVec2(-1.0f, 0.0f));
                ImGui::Text("Completed %d / %d",
                    startupState.Progress.readyRequired + startupState.Progress.failedRequired,
                    startupState.Progress.totalRequired);
                if (!startupState.Progress.currentLabel.empty()) {
                    ImGui::TextWrapped("Current: %s", startupState.Progress.currentLabel.c_str());
                }
                ImGui::Text("Elapsed: %.1fs / %.1fs",
                    startupState.ElapsedSec,
                    (std::max)(1.0f, startupConfig.TimeoutSeconds));
                ImGui::End();
            }
    }

    void ShowcaseRuntime::Shutdown() {
            RenderUtils::ShaderSystem::UnregisterUniformResolver("app.mouse_norm");
            RenderUtils::ShaderSystem::UnregisterUniformResolver("app.sin_time");
            RenderUtils::UIRenderer::Shutdown(g_registry);
    }
}





