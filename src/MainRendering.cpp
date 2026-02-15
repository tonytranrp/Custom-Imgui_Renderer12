#include "MainRendering.hpp"
#include "Dx12Init/Dx12Init.hpp"
#include "Render/ImguiRender.hpp"
#include "Render/RenderUtils/UIRenderer.hpp"
#include "Render/RenderUtils/UIComponents.hpp"
#include "Render/RenderUtils/UIBuilder.hpp"
#include "RustComponents/RustBridge.hpp"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include <tchar.h>
#include <cassert>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace MainRendering {

    // EnTT Registry for UI Entities
    entt::registry g_registry;

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

    // State for Tabs
    enum class TabCategory { Combat, Movement, Misc };
    static TabCategory g_CurrentTab = TabCategory::Combat;

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

        RenderUtils::UIRenderer::Init(g_registry);

        // --- UI Construction ---
        RenderUtils::UIBuilder::Begin(g_registry)
            // 1. Full Screen Background
            .Create<RenderUtils::ContainerType::Panel>("MainContainer")
                .With<RenderUtils::TransformComponent>(
                    RenderUtils::TransformComponent()
                        .SetPosition(ImVec2(0, 0))
                        .SetSize(ImVec2(1600, 900))
                )
                .With<RenderUtils::StyleComponent>(
                    RenderUtils::StyleComponent()
                        .SetBackgroundColor(IM_COL32(30, 30, 30, 255))
                        .SetLayer(RenderUtils::ZOrder::Background)
                )
                .With<RenderUtils::LockedComponent>(
                    RenderUtils::LockedComponent().SetLocked(true)
                )
            .End()

            // 2. Main Cheat Window
            .Create<RenderUtils::ContainerType::Window>("CheatClient")
                .With<RenderUtils::TransformComponent>(
                    RenderUtils::TransformComponent()
                        .SetPosition(ImVec2(100, 100))
                        .SetSize(ImVec2(800, 500))
                )
                .With<RenderUtils::StyleComponent>(
                    RenderUtils::StyleComponent()
                        .SetBackgroundColor(IM_COL32(40, 40, 45, 255))
                        .SetBorderColor(static_cast<ImU32>(RenderUtils::ColorPreset::WindowBorder))
                        .SetBorderSize(1.0f)
                        .SetRounding(10.0f)
                        .SetLayer(RenderUtils::ZOrder::Normal)
                )
                .With<RenderUtils::DraggableComponent>(
                    RenderUtils::DraggableComponent().SetMode(RenderUtils::DragMode::Free)
                )
                // Removed Global ClipChildren to allow Icon to be visible in Header
                // .With<RenderUtils::ClipComponent>(RenderUtils::ClipComponent().SetClipChildren(true))


                // --- Sidebar (Left) ---
                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("Sidebar")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent()
                                .SetPosition(ImVec2(0, 40)) // Below Header
                                .SetSize(ImVec2(200, 460))
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent()
                                .SetBackgroundColor(IM_COL32(35, 35, 40, 255))
                                .SetBorderColor(IM_COL32(60, 60, 60, 255))
                                .SetBorderSize(1.0f)
                        )
                        .With<RenderUtils::ClipComponent>(RenderUtils::ClipComponent().SetClipChildren(true))
                        
                        // Button: Combat
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Button>("BtnCombat")
                                .With<RenderUtils::TransformComponent>(
                                    RenderUtils::TransformComponent()
                                        .SetPosition(ImVec2(10, 20))
                                        .SetSize(ImVec2(180, 40))
                                )
                                .With<RenderUtils::StyleComponent>(
                                    RenderUtils::StyleComponent()
                                        .SetRounding(5.0f)
                                        .SetBackgroundColor(IM_COL32(60, 60, 70, 255))
                                )
                                .IsTabTrigger((int)TabCategory::Combat)
                                .With<RenderUtils::TextComponent>(
                                    RenderUtils::TextComponent("Combat", IM_COL32(255, 255, 255, 255))
                                        .Align(RenderUtils::TextAlign::Center)
                                )
                        )

                        // Button: Movement
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Button>("BtnMovement")
                                .With<RenderUtils::TransformComponent>(
                                    RenderUtils::TransformComponent()
                                        .SetPosition(ImVec2(10, 70))
                                        .SetSize(ImVec2(180, 40))
                                )
                                .With<RenderUtils::StyleComponent>(
                                    RenderUtils::StyleComponent()
                                        .SetRounding(5.0f)
                                        .SetBackgroundColor(IM_COL32(60, 60, 70, 255))
                                )
                                .IsTabTrigger((int)TabCategory::Movement)
                                .With<RenderUtils::TextComponent>(
                                    RenderUtils::TextComponent("Movement", IM_COL32(255, 255, 255, 255))
                                        .Align(RenderUtils::TextAlign::Center)
                                )
                        )

                        // Button: Misc
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Button>("BtnMisc")
                                .With<RenderUtils::TransformComponent>(
                                    RenderUtils::TransformComponent()
                                        .SetPosition(ImVec2(10, 120))
                                        .SetSize(ImVec2(180, 40))
                                )
                                .With<RenderUtils::StyleComponent>(
                                    RenderUtils::StyleComponent()
                                        .SetRounding(5.0f)
                                        .SetBackgroundColor(IM_COL32(60, 60, 70, 255))
                                )
                                .IsTabTrigger((int)TabCategory::Misc)
                                .With<RenderUtils::TextComponent>(
                                    RenderUtils::TextComponent("Misc", IM_COL32(255, 255, 255, 255))
                                        .Align(RenderUtils::TextAlign::Center)
                                )
                        )
                )

                // --- Content Area (Right) ---
                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("ContentArea")
                        .With<RenderUtils::ClipComponent>(RenderUtils::ClipComponent().SetClipChildren(true))
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent()
                                .SetPosition(ImVec2(200, 40))
                                .SetSize(ImVec2(600, 460))
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent()
                                .SetBackgroundColor(IM_COL32(0, 0, 0, 0)) // Transparent
                        )
                        .With<RenderUtils::ClipComponent>(RenderUtils::ClipComponent().SetClipChildren(true))

                        // Group: Combat
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Panel>("GroupCombat")
                                .With<RenderUtils::TransformComponent>(
                                    RenderUtils::TransformComponent().SetPosition(ImVec2(0, 0)).SetSize(ImVec2(600, 460))
                                )
                                .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetVisible(true)) // Default Visible
                                .IsTab((int)TabCategory::Combat)
                                // Module: Killaura
                                .Child(
                                    RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("ModKillaura")
                                        .With<RenderUtils::TransformComponent>(
                                            RenderUtils::TransformComponent().SetPosition(ImVec2(20, 20)).SetSize(ImVec2(560, 50))
                                        )
                                        .With<RenderUtils::StyleComponent>(
                                            RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(50, 50, 60, 255)).SetRounding(5.0f)
                                        )
                                        .With<RenderUtils::TextComponent>(
                                            RenderUtils::TextComponent("Killaura", IM_COL32(255, 200, 200, 255)).Align(RenderUtils::TextAlign::Left)
                                        )
                                        .With<RenderUtils::InputStateComponent>()
                                        // Toggle Button (Custom)
                                        .With<RenderUtils::CustomComponent>(
                                            RenderUtils::CustomComponent().SetOnRender([](entt::registry& reg, entt::entity e, ImDrawList* dl, ImVec2 p_min, ImVec2 p_max, bool hovered, bool clicked) {
                                                static bool enabled = false;
                                                
                                                // Toggle on Click
                                                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                                    enabled = !enabled;
                                                    
                                                    // Use AnimationBuilder
                                                    RenderUtils::AnimationBuilder builder(reg, e);
                                                    
                                                    if (enabled) {
                                                        // Add Glow
                                                        if (!reg.any_of<RenderUtils::GlowComponent>(e)) {
                                                            reg.emplace<RenderUtils::GlowComponent>(e, IM_COL32(0, 255, 0, 0), 15.0f, 0.0f);
                                                        }
                                                        
                                                        // Start Animations (Glow Pulse + Rainbow Text)
                                                        builder
                                                            .Loop(true)
                                                            .Duration(2.0f)
                                                            .Ease(RenderUtils::EasingType::Linear)
                                                            .Custom([](float t, entt::registry& r, entt::entity ent) {
                                                                // 1. Glow Pulse
                                                                if (r.all_of<RenderUtils::GlowComponent>(ent)) {
                                                                    auto& glow = r.get<RenderUtils::GlowComponent>(ent);
                                                                    // Pulse Intensity 0.5 to 1.0
                                                                    float pulse = 0.5f + 0.5f * sinf(t * 6.28f);
                                                                    glow.Intensity = pulse;
                                                                    // Color Cycle (Green)
                                                                    glow.Color = IM_COL32(0, 255, 0, 255);
                                                                }

                                                                if (r.all_of<RenderUtils::TextComponent>(ent)) {
                                                                    auto& tc = r.get<RenderUtils::TextComponent>(ent);
                                                                    
                                                                    // Per-Character Animation Callback
                                                                    tc.CharacterTransformCallback = [t](int index, char c, ImVec2& pos, float& rotation, ImU32& color, float& scale) {
                                                                        // 1. Rainbow Color
                                                                        float hue = t + (float)index * 0.1f;
                                                                        if (hue > 1.0f) hue -= 1.0f;
                                                                        ImVec4 col = ImColor::HSV(hue, 1.0f, 1.0f);
                                                                        color = ImColor(col);

                                                                        // 2. Rotation (Spinning)
                                                                        rotation = t * 360.0f; 
                                                                        
                                                                        // 3. Wave Effect (Offset Y)
                                                                        pos.y += sinf((t * 6.28f) + (index * 0.5f)) * 5.0f;
                                                                    };
                                                                }
                                                            })
                                                            .Start();
                                                    } else {
                                                        // Stop/Reset
                                                        if (reg.any_of<RenderUtils::AnimationComponent>(e)) {
                                                            reg.remove<RenderUtils::AnimationComponent>(e);
                                                        }
                                                        if (reg.any_of<RenderUtils::GlowComponent>(e)) {
                                                            reg.remove<RenderUtils::GlowComponent>(e);
                                                        }
                                                        if (reg.all_of<RenderUtils::TextComponent>(e)) {
                                                            auto& tc = reg.get<RenderUtils::TextComponent>(e);
                                                            tc.CharacterTransformCallback = nullptr; // Clear callback
                                                            tc.Color = IM_COL32(255, 200, 200, 255); // Reset color
                                                        }
                                                    }
                                                }
                                                
                                                // Draw Toggle Status Indicator
                                                float radius = 6.0f;
                                                ImVec2 center = ImVec2(p_max.x - 20, p_min.y + (p_max.y - p_min.y) * 0.5f);
                                                dl->AddCircleFilled(center, radius, enabled ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255));
                                            })
                                        )
                                )
                                // Demo: Slider for HUD Scale
                                .Child(
                                    RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("ModScale")
                                        .With<RenderUtils::TransformComponent>(
                                            RenderUtils::TransformComponent().SetPosition(ImVec2(20, 80)).SetSize(ImVec2(560, 50))
                                        )
                                        .With<RenderUtils::StyleComponent>(
                                            RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(50, 50, 60, 255)).SetRounding(5.0f)
                                        )
                                        .With<RenderUtils::TextComponent>(
                                            RenderUtils::TextComponent("HUD Scale", IM_COL32(200, 200, 255, 255)).Align(RenderUtils::TextAlign::Left)
                                        )
                                        .With<RenderUtils::SliderComponent>(
                                            RenderUtils::SliderComponent(1.0f, 0.5f, 2.0f)
                                                .SetSmoothing(true, 15.0f) // Enable smoothing
                                                .SetColors(IM_COL32(60, 60, 70, 255), IM_COL32(100, 200, 100, 255), IM_COL32(255, 255, 255, 255))
                                                .SetSizes(6.0f, 10.0f)
                                                .SetOnChange([](float val) {
                                                    // Callback
                                                })
                                        )
                                        .With<RenderUtils::InputStateComponent>(RenderUtils::InputStateComponent().SetBlockInput(false))
                                )
                                // Demo: Text Input for Config Name
                                .Child(
                                    RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("ModConfig")
                                        .With<RenderUtils::TransformComponent>(
                                            RenderUtils::TransformComponent().SetPosition(ImVec2(20, 140)).SetSize(ImVec2(560, 50))
                                        )
                                        .With<RenderUtils::StyleComponent>(
                                            RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(50, 50, 60, 255)).SetRounding(5.0f)
                                        )
                                        .With<RenderUtils::TextComponent>(
                                            RenderUtils::TextComponent("Config Name", IM_COL32(200, 200, 255, 255)).Align(RenderUtils::TextAlign::Left)
                                        )
                                        .Child(
                                            RenderUtils::UIBuilder::Begin(g_registry)
                                                .Create<RenderUtils::ContainerType::Panel>("InputConfig")
                                                .With<RenderUtils::TransformComponent>(
                                                     RenderUtils::TransformComponent().SetPosition(ImVec2(400, 10)).SetSize(ImVec2(140, 30))
                                                )
                                                .With<RenderUtils::TextInputComponent>(
                                                    RenderUtils::TextInputComponent("Enter name...", 32).SetBuffer("Default")
                                                )
                                                .With<RenderUtils::StyleComponent>(
                                                     RenderUtils::StyleComponent().SetRounding(3.0f)
                                                )
                                        )
                                        .With<RenderUtils::InputStateComponent>(RenderUtils::InputStateComponent().SetBlockInput(false))
                                )
                        )

                        // Group: Movement
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Panel>("GroupMovement")
                                .With<RenderUtils::TransformComponent>(
                                    RenderUtils::TransformComponent().SetPosition(ImVec2(0, 0)).SetSize(ImVec2(600, 460))
                                )
                                .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetVisible(false))
                                .IsTab((int)TabCategory::Movement)
                                // Module: Speed
                                .Child(
                                    RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("ModSpeed")
                                        .With<RenderUtils::TransformComponent>(
                                            RenderUtils::TransformComponent().SetPosition(ImVec2(20, 20)).SetSize(ImVec2(560, 50))
                                        )
                                        .With<RenderUtils::StyleComponent>(
                                            RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(50, 50, 60, 255)).SetRounding(5.0f)
                                        )
                                        .With<RenderUtils::TextComponent>(
                                            RenderUtils::TextComponent("Speed", IM_COL32(200, 255, 255, 255)).Align(RenderUtils::TextAlign::Left)
                                        )
                                        .With<RenderUtils::CustomComponent>(
                                            RenderUtils::CustomComponent().SetOnRender([](entt::registry&, entt::entity, ImDrawList* dl, ImVec2 p_min, ImVec2 p_max, bool, bool clicked) {
                                                static bool enabled = false;
                                                if (clicked && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) enabled = !enabled;
                                                
                                                float radius = 10.0f;
                                                ImVec2 center = ImVec2(p_max.x - 30, p_min.y + 25);
                                                dl->AddCircleFilled(center, radius, enabled ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255));
                                                dl->AddCircle(center, radius, IM_COL32(255,255,255,255));
                                            })
                                        )
                                        .With<RenderUtils::InputStateComponent>(RenderUtils::InputStateComponent().SetBlockInput(false))
                                )
                        )

                        // Group: Misc
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Panel>("GroupMisc")
                                .With<RenderUtils::TransformComponent>(
                                    RenderUtils::TransformComponent().SetPosition(ImVec2(0, 0)).SetSize(ImVec2(600, 460))
                                )
                                .With<RenderUtils::StyleComponent>(RenderUtils::StyleComponent().SetVisible(false))
                                .IsTab((int)TabCategory::Misc)
                                // Module: Gui
                                .Child(
                                    RenderUtils::UIBuilder::Begin(g_registry)
                                        .Create<RenderUtils::ContainerType::Panel>("ModGui")
                                        .With<RenderUtils::TransformComponent>(
                                            RenderUtils::TransformComponent().SetPosition(ImVec2(20, 20)).SetSize(ImVec2(560, 50))
                                        )
                                        .With<RenderUtils::StyleComponent>(
                                            RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(50, 50, 60, 255)).SetRounding(5.0f)
                                        )
                                        .With<RenderUtils::TextComponent>(
                                            RenderUtils::TextComponent("Gui", IM_COL32(255, 255, 200, 255)).Align(RenderUtils::TextAlign::Left)
                                        )
                                        .With<RenderUtils::CustomComponent>(
                                            RenderUtils::CustomComponent().SetOnRender([](entt::registry&, entt::entity, ImDrawList* dl, ImVec2 p_min, ImVec2 p_max, bool, bool clicked) {
                                                static bool enabled = true; // Default ON
                                                if (clicked && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) enabled = !enabled;
                                                
                                                float radius = 10.0f;
                                                ImVec2 center = ImVec2(p_max.x - 30, p_min.y + 25);
                                                dl->AddCircleFilled(center, radius, enabled ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255));
                                                dl->AddCircle(center, radius, IM_COL32(255,255,255,255));
                                            })
                                        )
                                        .With<RenderUtils::InputStateComponent>(RenderUtils::InputStateComponent().SetBlockInput(false))
                                )
                        )
                )
            .End()

            // 3. Settings Window (Kept as requested)
            .Create<RenderUtils::ContainerType::Window>("Settings")
                .With<RenderUtils::TransformComponent>(
                    RenderUtils::TransformComponent()
                        .SetPosition(ImVec2(800, 50))
                        .SetSize(ImVec2(300, 400))
                )
                .With<RenderUtils::StyleComponent>(
                    RenderUtils::StyleComponent()
                        .SetBackgroundColor(IM_COL32(35, 35, 40, 255))
                        .SetBorderColor(static_cast<ImU32>(RenderUtils::ColorPreset::WindowBorder))
                        .SetBorderSize(1.0f)
                        .SetRounding(8.0f)
                        .SetLayer(RenderUtils::ZOrder::Top) // Always on top
                )
                .With<RenderUtils::DraggableComponent>(
                    RenderUtils::DraggableComponent().SetMode(RenderUtils::DragMode::Free)
                )
                
                // Opacity Slider
                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("OpacityControl")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent()
                                .SetPosition(ImVec2(20, 50))
                                .SetSize(ImVec2(260, 60))
                        )
                        .With<RenderUtils::TextComponent>(
                            RenderUtils::TextComponent("Window Opacity", IM_COL32(200, 200, 200, 255))
                        )
                        .With<RenderUtils::SliderComponent>(
                            RenderUtils::SliderComponent(1.0f, 0.1f, 1.0f)
                                .SetOnChange([](float val) {
                                    // Find the Main Window and update transparency
                                    auto target = RenderUtils::UIRenderer::FindEntityByName(g_registry, "Rich Text Demo");
                                    if (g_registry.valid(target)) {
                                        if (g_registry.any_of<RenderUtils::TransparencyComponent>(target)) {
                                            g_registry.get<RenderUtils::TransparencyComponent>(target).Alpha = val;
                                        }
                                    }
                                })
                        )
                )

                // Expandable Info Section
                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("InfoSection")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent()
                                .SetPosition(ImVec2(20, 130))
                                .SetSize(ImVec2(260, 40)) // Initial small size
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent()
                                .SetBackgroundColor(IM_COL32(50, 50, 60, 255))
                                .SetRounding(4.0f)
                        )
                        .With<RenderUtils::ExpandComponent>(
                            RenderUtils::ExpandComponent()
                                .SetExpanded(false)
                                .SetExpandedHeight(150.0f)
                        )
                        .With<RenderUtils::TextComponent>(
                            RenderUtils::TextComponent("Click to Expand Info...", IM_COL32(255, 255, 100, 255))
                                .Align(RenderUtils::TextAlign::Center)
                        )
                        // Click handler to toggle expand
                        .With<RenderUtils::CustomComponent>(
                            RenderUtils::CustomComponent()
                                .SetOnRender([](entt::registry& reg, entt::entity e, ImDrawList*, const ImVec2&, const ImVec2&, bool hovered, bool clicked) {
                                    if (clicked && reg.any_of<RenderUtils::ExpandComponent>(e)) {
                                        auto& exp = reg.get<RenderUtils::ExpandComponent>(e);
                                        // Simple toggle with debounce (naive)
                                        // In real app, check mouse released. But here we rely on UIRenderer handling clicked state per frame.
                                        // Since UIRenderer sets Clicked=true only on first frame of click usually?
                                        // Actually UIRenderer sets IsClicked = true while mouse is down.
                                        // We need "Just Clicked".
                                        // For now, let's assume user holds it. This might flicker.
                                        // Better: Use ImGui::IsMouseClicked inside here?
                                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                            exp.IsExpanded = !exp.IsExpanded;
                                            // Update size
                                            if (reg.all_of<RenderUtils::TransformComponent>(e)) {
                                                auto& t = reg.get<RenderUtils::TransformComponent>(e);
                                                t.Size.y = exp.IsExpanded ? exp.ExpandedHeight : 40.0f;
                                            }
                                        }
                                    }
                                })
                        )
                )
            .End();

        // ------------------------------------------------

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
            // We must open the command list BEFORE UIRenderer::Render because ImageLoader might need to record upload commands.
            FrameContext* frameCtx = DX12Init::WaitForNextFrameResources();
            UINT backBufferIdx = DX12Init::g_pSwapChain->GetCurrentBackBufferIndex();
            frameCtx->CommandAllocator->Reset();
            DX12Init::g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);

            // --- 2. ImGui Frame Setup ---
            ImguiRender::NewFrame();

            // Update Main Container Size
            ImGuiIO& io = ImGui::GetIO();
            // Since we don't have the "mainContainer" variable easily accessible (it was local to builder),
            // we should find it by name.
            auto mainEnt = RenderUtils::UIRenderer::FindEntityByName(g_registry, "MainContainer");
            if (g_registry.valid(mainEnt)) {
                g_registry.patch<RenderUtils::TransformComponent>(mainEnt, [&io](auto& transform) {
                    transform.Size = io.DisplaySize;
                });
            }

            // --- Logic for Tabs ---
            // Update visibility of groups based on g_CurrentTab
            // Use TabSwitchComponent to manage visibility automatically
            // auto tabView = g_registry.view<RenderUtils::TabSwitchComponent, RenderUtils::StyleComponent>();
            // ... (Removed manual logic) ...

            // Update Sidebar Button Colors (Highlight Active)
            // ... (Removed manual logic) ...

            // Update Library Systems
            RenderUtils::UIRenderer::Update(g_registry, 1.0f / io.Framerate);

            // Render Custom UI (This might trigger texture uploads using g_pd3dCommandList)
            RenderUtils::UIRenderer::Render(g_registry);

            // Render Inspector if Debug Mode is on
            if (RenderUtils::UIRenderer::DebugMode) {
                RenderUtils::UIRenderer::RenderInspector(g_registry);
            }

            // Debug UI for Toggling Drag Mode
            ImGui::SetNextWindowPos(ImVec2(10, io.DisplaySize.y - 60));
            ImGui::SetNextWindowSize(ImVec2(300, 50));
            ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
            if (ImGui::Button(RenderUtils::UIRenderer::DebugMode ? "Disable Debug Drag" : "Enable Debug Drag")) {
                RenderUtils::UIRenderer::DebugMode = !RenderUtils::UIRenderer::DebugMode;
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0, 1), RenderUtils::UIRenderer::DebugMode ? "[DEBUG MODE ON]" : "");
            ImGui::End();

            // Optional: Standard ImGui Debug Window
            ImGui::Begin("Debug Info");
            ImGui::Text("Entities: %d", g_registry.storage<entt::entity>().size());
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::End();

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
        ImguiRender::Cleanup();
        DX12Init::CleanupDeviceD3D();
        DestroyWindow(hWnd);
        UnregisterClass(wc.lpszClassName, wc.hInstance);

        return 0;
    }
}
