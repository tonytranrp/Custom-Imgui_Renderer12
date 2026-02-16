#include "MainRendering.hpp"
#include "Dx12Init/Dx12Init.hpp"
#include "Render/ImguiRender.hpp"
#include "Render/RenderUtils/UIRenderer.hpp"
#include "Render/RenderUtils/UIComponents.hpp"
#include "Render/RenderUtils/UIBuilder.hpp"
#include "Render/RenderUtils/ShaderSystem.hpp"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include <tchar.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace MainRendering {

    // EnTT Registry for UI Entities
    entt::registry g_registry;

    namespace {
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

        enum class StartupMode {
            BlockOnRequiredImages = 0,
            ImmediateUI = 1
        };

        enum class StartupPriority {
            ImagesFirst = 0,
            SystemsFirst = 1,
            Balanced = 2
        };

        struct StartupConfig {
            StartupMode Mode = StartupMode::BlockOnRequiredImages;
            StartupPriority Priority = StartupPriority::ImagesFirst;
            float TimeoutSeconds = 15.0f;
        };

        struct StartupState {
            bool Initialized = false;
            bool Completed = false;
            bool TimedOut = false;
            float StartTimeSec = 0.0f;
            float ElapsedSec = 0.0f;
            Components::ImageLoaderProgress Progress;
            std::string SummaryLine;
        };
    }

    LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;

        switch (msg) {
        case WM_SIZE:
            if (DX12Init::g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
                DX12Init::ResizeSwapChain(hWnd, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam));
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    int Run(HINSTANCE hInstance) {
        WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, _T("ImGui DX12 Class"), nullptr };
        RegisterClassEx(&wc);
        HWND hWnd = CreateWindow(wc.lpszClassName, _T("ImGui DX12 App"), WS_OVERLAPPEDWINDOW, 100, 100, 1600, 900, nullptr, nullptr, wc.hInstance, nullptr);

        if (!DX12Init::CreateDeviceD3D(hWnd)) {
            DX12Init::CleanupDeviceD3D();
            UnregisterClass(wc.lpszClassName, wc.hInstance);
            return 1;
        }

        ShowWindow(hWnd, SW_SHOWDEFAULT);
        UpdateWindow(hWnd);

        ImguiRender::Init(hWnd, DX12Init::g_pd3dDevice, DX12Init::NUM_FRAMES_IN_FLIGHT,
            DXGI_FORMAT_R8G8B8A8_UNORM, DX12Init::g_pd3dSrvDescHeap, DX12Init::g_pd3dCommandQueue);

        RenderUtils::ShaderSystem::Initialize(DX12Init::g_pd3dDevice);
        RenderUtils::UIRenderer::Init(g_registry);


        // --- UI Construction ---
        RenderUtils::UIRenderer::SetCurrentTab(static_cast<int>(DemoTab::Overview));
        RenderUtils::UIBuilder::Begin(g_registry)
            .Create<RenderUtils::ContainerType::Panel>("MainContainer")
                .With<RenderUtils::TransformComponent>(
                    RenderUtils::TransformComponent().SetPosition(ImVec2(0, 0)).SetSize(ImVec2(1600, 900))
                )
                .With<RenderUtils::StyleComponent>(
                    RenderUtils::StyleComponent()
                        .SetBackgroundColor(IM_COL32(16, 18, 22, 255))
                        .SetGradient(true, IM_COL32(24, 27, 34, 255), IM_COL32(10, 12, 16, 255))
                        .SetLayer(RenderUtils::ZOrder::Background)
                )
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
                                    .Create<RenderUtils::ContainerType::Panel>("OverviewTransparencyBackdrop")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(18, 414)).SetSize(ImVec2(310, 120)))
                                    .With<RenderUtils::StyleComponent>(
                                        RenderUtils::StyleComponent()
                                            .SetBackgroundColor(IM_COL32(26, 30, 38, 255))
                                            .SetGradient(true, IM_COL32(86, 92, 112, 255), IM_COL32(22, 27, 36, 255))
                                            .SetRounding(8.0f)
                                            .SetBorderColor(IM_COL32(78, 88, 110, 255))
                                            .SetBorderSize(1.0f))
                                    .With<RenderUtils::TextComponent>(
                                        RenderUtils::TextComponent("Background reference", IM_COL32(198, 210, 232, 255)).Align(RenderUtils::TextAlign::Center)))
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("OverviewTransparency")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(34, 432)).SetSize(ImVec2(278, 90)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(74, 174, 255, 255)).SetRounding(8.0f))
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
                                        .With<RenderUtils::ShadowComponent>(RenderUtils::ShadowComponent().SetColor(IM_COL32(0, 0, 0, 135)).SetOffset(ImVec2(10, 12)).SetBlurRadius(22.0f).SetSpread(2.5f).SetSamples(22))
                                        .With<RenderUtils::ShaderComponent>(RenderUtils::ShaderComponent().SetPixelSource(RenderUtils::ShaderSourceSpec().SetMode(RenderUtils::ShaderSourceMode::File).SetSource("assets/shaders/shadow_default.hlsl").SetTarget("ps_5_0")))
                                        .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Object A", IM_COL32(255, 255, 255, 255)))
                                    )
                                )
                                .Child(RenderUtils::UIBuilder::Begin(g_registry)
                                    .Create<RenderUtils::ContainerType::Panel>("EffectsGlowStage")
                                    .With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(648, 18)).SetSize(ImVec2(584, 360)))
                                    .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(24, 27, 34, 255)).SetRounding(8.0f))
                                    .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Glow Modes", IM_COL32(220, 230, 245, 255)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry).Create<RenderUtils::ContainerType::Panel>("GlowGaussianCard").With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(20, 70)).SetSize(ImVec2(170, 230))).With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(48, 54, 64, 255)).SetRounding(10.0f)).With<RenderUtils::GlowComponent>(RenderUtils::GlowComponent(IM_COL32(102, 180, 255, 255), 34.0f, 1.6f).SetMode(RenderUtils::GlowMode::GaussianBloom).SetRenderMode(RenderUtils::GlowRenderMode::Shader)).With<RenderUtils::ShaderComponent>(RenderUtils::ShaderComponent().SetPixelSource(RenderUtils::ShaderSourceSpec().SetMode(RenderUtils::ShaderSourceMode::File).SetSource("assets/shaders/glow_default.hlsl").SetTarget("ps_5_0"))).With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Gaussian", IM_COL32(235, 245, 255, 255)).Align(RenderUtils::TextAlign::Center)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry).Create<RenderUtils::ContainerType::Panel>("GlowNeonCard").With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(206, 70)).SetSize(ImVec2(170, 230))).With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(45, 52, 61, 255)).SetRounding(10.0f)).With<RenderUtils::GlowComponent>(RenderUtils::GlowComponent(IM_COL32(88, 245, 225, 255), 36.0f, 1.9f).SetMode(RenderUtils::GlowMode::NeonTube).SetCoreStrength(1.1f).SetInnerGlow(true).SetRenderMode(RenderUtils::GlowRenderMode::Shader)).With<RenderUtils::ShaderComponent>(RenderUtils::ShaderComponent().SetPixelSource(RenderUtils::ShaderSourceSpec().SetMode(RenderUtils::ShaderSourceMode::File).SetSource("assets/shaders/glow_default.hlsl").SetTarget("ps_5_0"))).With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Neon", IM_COL32(235, 245, 255, 255)).Align(RenderUtils::TextAlign::Center)))
                                    .Child(RenderUtils::UIBuilder::Begin(g_registry).Create<RenderUtils::ContainerType::Panel>("GlowAmbientCard").With<RenderUtils::TransformComponent>(RenderUtils::TransformComponent().SetPosition(ImVec2(392, 70)).SetSize(ImVec2(170, 230))).With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(47, 53, 63, 255)).SetRounding(10.0f)).With<RenderUtils::GlowComponent>(RenderUtils::GlowComponent(IM_COL32(198, 160, 255, 255), 42.0f, 1.35f).SetMode(RenderUtils::GlowMode::AmbientSoft).SetRenderMode(RenderUtils::GlowRenderMode::Shader)).With<RenderUtils::ShaderComponent>(RenderUtils::ShaderComponent().SetPixelSource(RenderUtils::ShaderSourceSpec().SetMode(RenderUtils::ShaderSourceMode::File).SetSource("assets/shaders/glow_default.hlsl").SetTarget("ps_5_0"))).With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Ambient", IM_COL32(235, 245, 255, 255)).Align(RenderUtils::TextAlign::Center)))
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
                                    .With<RenderUtils::ShaderComponent>(RenderUtils::ShaderComponent().SetPixelSource(RenderUtils::ShaderSourceSpec().SetMode(RenderUtils::ShaderSourceMode::File).SetSource("assets/shaders/glow_default.hlsl").SetTarget("ps_5_0")))
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
                                                        RenderUtils::GlowComponent(IM_COL32(88, 245, 225, 255), 22.0f, 0.8f)
                                                            .SetMode(RenderUtils::GlowMode::NeonTube)
                                                            .SetCoreStrength(0.65f)
                                                            .SetInnerGlow(true)
                                                            .SetSamples(14));

                                                    RenderUtils::AnimationBuilder(reg, e)
                                                        .Loop(true)
                                                        .Duration(1.6f)
                                                        .Ease(RenderUtils::EasingType::EaseInOutQuad)
                                                        .Custom([](float t, entt::registry& r, entt::entity ent) {
                                                            if (!r.valid(ent) || !r.any_of<RenderUtils::GlowComponent>(ent)) {
                                                                return;
                                                            }
                                                            auto& glow = r.get<RenderUtils::GlowComponent>(ent);
                                                            glow.SetIntensity(0.35f + 0.7f * std::fabs(std::sin(t * 6.28318f)));
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
                                        .With<RenderUtils::OptionsComponent>(RenderUtils::OptionsComponent({ "Block On Required Images", "Immediate UI" }, 0))
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


        StartupConfig startupConfig;
        StartupState startupState;

        bool done = false;
        while (!done) {
            MSG msg;
            while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (msg.message == WM_QUIT)
                    done = true;
            }
            if (done) break;

            // --- 1. Begin Frame & Open Command List ---
            // We must open the command list BEFORE UIRenderer::Update because ImageLoaderSystem may record upload commands.
            FrameContext* frameCtx = DX12Init::WaitForNextFrameResources();
            UINT backBufferIdx = DX12Init::g_pSwapChain->GetCurrentBackBufferIndex();
            frameCtx->CommandAllocator->Reset();
            DX12Init::g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);

            // --- 2. ImGui Frame Setup ---
            ImguiRender::NewFrame();

            // Update Main Container Size
            ImGuiIO& io = ImGui::GetIO();
            RenderUtils::ShaderSystem::BeginImGuiPass(
                DX12Init::g_pd3dCommandList,
                DX12Init::g_mainRenderTargetDescriptor[backBufferIdx],
                io.DisplaySize);
            // Since we don't have the "mainContainer" variable easily accessible (it was local to builder),
            // we should find it by name.
            auto mainEnt = RenderUtils::UIRenderer::FindEntityByName(g_registry, "MainContainer");
            if (g_registry.valid(mainEnt)) {
                g_registry.patch<RenderUtils::TransformComponent>(mainEnt, [&io](auto& transform) {
                    transform.Size = io.DisplaySize;
                });
            }

            float deltaTime = 1.0f / 60.0f;
            if (std::isfinite(io.Framerate) && io.Framerate > 1.0f) {
                deltaTime = 1.0f / io.Framerate;
            }
            if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
                deltaTime = 1.0f / 60.0f;
            }
            if (deltaTime > 0.25f) {
                deltaTime = 0.25f;
            }
            bool ranMainUpdate = false;

            if (!startupState.Initialized) {
                startupState.Initialized = true;
                startupState.Completed = (startupConfig.Mode == StartupMode::ImmediateUI);
                startupState.TimedOut = false;
                startupState.StartTimeSec = static_cast<float>(ImGui::GetTime());
                startupState.ElapsedSec = 0.0f;
                startupState.SummaryLine.clear();
            }

            if (startupConfig.Mode == StartupMode::BlockOnRequiredImages && !startupState.Completed) {
                if (startupConfig.Priority == StartupPriority::ImagesFirst) {
                    Components::ImageLoaderSystem::Update(g_registry);
                    RenderUtils::UIRenderer::ResolveTransforms(g_registry);
                    RenderUtils::UIRenderer::ResolveDepth(g_registry);
                } else {
                    RenderUtils::UIRenderer::Update(g_registry, deltaTime);
                }
                ranMainUpdate = true;

                startupState.Progress = Components::ImageLoaderSystem::QueryProgress(g_registry);
                startupState.ElapsedSec = static_cast<float>(ImGui::GetTime()) - startupState.StartTimeSec;

                if (startupState.Progress.done) {
                    startupState.Completed = true;
                    startupState.SummaryLine =
                        "Startup complete: " + std::to_string(startupState.Progress.readyRequired) + " ready, " +
                        std::to_string(startupState.Progress.failedRequired) + " failed.";
                } else {
                    const float timeout = (std::max)(1.0f, startupConfig.TimeoutSeconds);
                    if (startupState.ElapsedSec >= timeout) {
                        startupState.Completed = true;
                        startupState.TimedOut = true;
                        startupState.SummaryLine =
                            "Startup timeout after " + std::to_string(static_cast<int>(timeout)) + "s: " +
                            std::to_string(startupState.Progress.readyRequired) + " ready, " +
                            std::to_string(startupState.Progress.failedRequired) + " failed, " +
                            std::to_string(startupState.Progress.loadingRequired) + " still loading.";
                    }
                }
            }

            if (!ranMainUpdate) {
                RenderUtils::UIRenderer::Update(g_registry, deltaTime);
                startupState.Progress = Components::ImageLoaderSystem::QueryProgress(g_registry);
            }

            auto findEntity = [](const char* name) -> entt::entity {
                return RenderUtils::UIRenderer::FindEntityByName(g_registry, name);
            };

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

                const StartupMode requestedMode = (selected == 1)
                    ? StartupMode::ImmediateUI
                    : StartupMode::BlockOnRequiredImages;
                if (requestedMode != startupConfig.Mode) {
                    startupConfig.Mode = requestedMode;
                    if (startupConfig.Mode == StartupMode::ImmediateUI) {
                        startupState.Completed = true;
                        startupState.TimedOut = false;
                        startupState.SummaryLine = "Startup mode switched to Immediate UI.";
                    } else {
                        startupState = StartupState{};
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
                startupConfig.Priority = static_cast<StartupPriority>(selected);
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

            RenderUtils::ShaderCompilePolicy shaderPolicy = RenderUtils::ShaderCompilePolicy::OnDemandCache;
            entt::entity shaderPolicyEnt = findEntity("SysShaderPolicyOptions");
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

            RenderUtils::ShaderBackendMode shaderBackend = RenderUtils::ShaderBackendMode::AutoPreferDXC;
            entt::entity shaderBackendEnt = findEntity("SysShaderBackendOptions");
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

            entt::entity glowModeEnt = findEntity("SysGlowRenderModeOptions");
            RenderUtils::GlowRenderMode glowRenderMode = RenderUtils::GlowRenderMode::Shader;
            if (g_registry.valid(glowModeEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(glowModeEnt)) {
                auto& options = g_registry.get<RenderUtils::OptionsComponent>(glowModeEnt);
                int selected = options.SelectedIndex;
                if (selected < 0) selected = 0;
                if (selected > 1) selected = 1;
                if (selected != options.SelectedIndex) {
                    options.SelectedIndex = selected;
                }
                glowRenderMode = selected == 1 ? RenderUtils::GlowRenderMode::Cpu : RenderUtils::GlowRenderMode::Shader;
            }

            entt::entity shadowModeEnt = findEntity("SysShadowRenderModeOptions");
            RenderUtils::ShadowRenderMode shadowRenderMode = RenderUtils::ShadowRenderMode::Cpu;
            if (g_registry.valid(shadowModeEnt) && g_registry.any_of<RenderUtils::OptionsComponent>(shadowModeEnt)) {
                auto& options = g_registry.get<RenderUtils::OptionsComponent>(shadowModeEnt);
                int selected = options.SelectedIndex;
                if (selected < 0) selected = 0;
                if (selected > 1) selected = 1;
                if (selected != options.SelectedIndex) {
                    options.SelectedIndex = selected;
                }
                shadowRenderMode = selected == 1 ? RenderUtils::ShadowRenderMode::Shader : RenderUtils::ShadowRenderMode::Cpu;
            }

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
                Components::ImageLoaderSystem::RequestRestart(g_registry, false);
                startupState = StartupState{};
                g_RequestStartupRestart = false;
            }

            if (g_RequestShaderRecompile) {
                auto shaders = g_registry.view<RenderUtils::ShaderComponent>();
                for (auto entity : shaders) {
                    auto& shader = shaders.get<RenderUtils::ShaderComponent>(entity);
                    shader.CompileRequested = true;
                    shader.Dirty = true;
                }
                g_RequestShaderRecompile = false;
            }

            const Components::ImageLoaderDebugStats loaderStats = Components::ImageLoaderSystem::QueryDebugStats(g_registry);

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
                std::string alphaText = std::to_string(alpha);
                const size_t dot = alphaText.find('.');
                if (dot != std::string::npos && dot + 3 < alphaText.size()) {
                    alphaText.resize(dot + 3);
                }
                auto& text = g_registry.get<RenderUtils::TextComponent>(transparencyEnt);
                text.RawText = "Transparency alpha: " + alphaText;
                text.Color = IM_COL32(12, 20, 30, 255);
            }

            const char* modeLabel = startupConfig.Mode == StartupMode::ImmediateUI
                ? "Immediate UI"
                : "Block On Required Images";
            const char* priorityLabel = startupConfig.Priority == StartupPriority::ImagesFirst
                ? "Images First"
                : (startupConfig.Priority == StartupPriority::SystemsFirst ? "Systems First" : "Balanced");

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
                "  Uploads: " + std::to_string(loaderStats.uploadedFramesLastTick) + " frames";
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

            if (startupConfig.Mode == StartupMode::BlockOnRequiredImages && !startupState.Completed) {
                const ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
                ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                ImGui::SetNextWindowSize(ImVec2(500, 170), ImGuiCond_Always);
                ImGui::Begin("Startup Loading Overlay", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
                ImGui::Text("Loading required images...");
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

            // --- 3. Finalize & Draw ---
            ImGui::Render();

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource   = DX12Init::g_mainRenderTargetResource[backBufferIdx];
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

            DX12Init::g_pd3dCommandList->ResourceBarrier(1, &barrier);

            const float clear_color_with_alpha[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
            DX12Init::g_pd3dCommandList->ClearRenderTargetView(DX12Init::g_mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
            DX12Init::g_pd3dCommandList->OMSetRenderTargets(1, &DX12Init::g_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
            DX12Init::g_pd3dCommandList->SetDescriptorHeaps(1, &DX12Init::g_pd3dSrvDescHeap);

            ImguiRender::RenderDrawData(DX12Init::g_pd3dCommandList);
            RenderUtils::ShaderSystem::EndImGuiPass();

            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
            DX12Init::g_pd3dCommandList->ResourceBarrier(1, &barrier);
            DX12Init::g_pd3dCommandList->Close();

            DX12Init::g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&DX12Init::g_pd3dCommandList);

            DX12Init::g_pSwapChain->Present(1, 0); // Present with vsync

            UINT64 fenceValue = DX12Init::g_fenceLastSignaledValue + 1;
            DX12Init::g_pd3dCommandQueue->Signal(DX12Init::g_fence, fenceValue);
            DX12Init::g_fenceLastSignaledValue = fenceValue;
            frameCtx->FenceValue = fenceValue;
        }

        DX12Init::WaitForLastSubmittedFrame();
        Components::ImageLoaderSystem::Shutdown(g_registry);
        RenderUtils::ShaderSystem::Shutdown();
        ImguiRender::Cleanup();
        DX12Init::CleanupDeviceD3D();
        DestroyWindow(hWnd);
        UnregisterClass(wc.lpszClassName, wc.hInstance);

        return 0;
    }
}


