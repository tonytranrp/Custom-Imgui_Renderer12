#include "MainRendering.hpp"
#include "Dx12Init/Dx12Init.hpp"
#include "Render/ImguiRender.hpp"
#include "Render/RenderUtils/UIRenderer.hpp"
#include "Render/RenderUtils/UIComponents.hpp"
#include "Render/RenderUtils/UIBuilder.hpp"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include <tchar.h>
#include <cassert>
#include <cmath>

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
            .Create<RenderUtils::ContainerType::Panel>("MainContainer")
                .With<RenderUtils::TransformComponent>(
                    RenderUtils::TransformComponent()
                        .SetPosition(ImVec2(0, 0))
                        .SetSize(ImVec2(1600, 900))
                )
                .With<RenderUtils::StyleComponent>(
                    RenderUtils::StyleComponent()
                        .SetBackgroundColor(IM_COL32(20, 22, 26, 255))
                        .SetGradient(true, IM_COL32(26, 28, 34, 255), IM_COL32(14, 16, 20, 255))
                        .SetLayer(RenderUtils::ZOrder::Background)
                )
                .With<RenderUtils::LockedComponent>(RenderUtils::LockedComponent().SetLocked(true))
            .End()

            .Create<RenderUtils::ContainerType::Window>("ShowcaseWindow")
                .With<RenderUtils::TransformComponent>(
                    RenderUtils::TransformComponent()
                        .SetPosition(ImVec2(60, 40))
                        .SetSize(ImVec2(1300, 780))
                )
                .With<RenderUtils::StyleComponent>(
                    RenderUtils::StyleComponent()
                        .SetBackgroundColor(IM_COL32(36, 39, 46, 255))
                        .SetBorderColor(static_cast<ImU32>(RenderUtils::ColorPreset::WindowBorder))
                        .SetBorderSize(1.0f)
                        .SetRounding(10.0f)
                        .SetLayer(RenderUtils::ZOrder::Normal)
                        .SetOutline(true, IM_COL32(110, 160, 220, 110), 1.5f)
                )
                .With<RenderUtils::WindowHeaderComponent>(
                    RenderUtils::WindowHeaderComponent()
                        .SetHeight(34.0f)
                        .SetPadding(12.0f, 9.0f)
                        .SetBackgroundColor(IM_COL32(45, 49, 58, 255))
                        .SetSeparatorColor(IM_COL32(15, 16, 20, 160))
                )
                .With<RenderUtils::DraggableComponent>(
                    RenderUtils::DraggableComponent().SetMode(RenderUtils::DragMode::Free)
                )
                .With<RenderUtils::ClipComponent>(RenderUtils::ClipComponent().SetClipChildren(true))

                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("ShapePanel")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent().SetPosition(ImVec2(20, 55)).SetSize(ImVec2(380, 210))
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent()
                                .SetBackgroundColor(IM_COL32(42, 45, 52, 255))
                                .SetRounding(8.0f)
                                .SetBorderColor(IM_COL32(72, 74, 90, 255))
                                .SetBorderSize(1.0f)
                                .SetContentPadding(14.0f, 14.0f)
                        )
                        .With<RenderUtils::TextComponent>(
                            RenderUtils::TextComponent("ShapeComponent: Rect + Circle + Triangle + Polyline", IM_COL32(210, 220, 255, 255))
                        )
                        .With<RenderUtils::ShapeComponent>(
                            []() {
                                RenderUtils::ShapeComponent shape;
                                shape.SetDrawBehindContent(false).SetClipToEntity(true).SetUseForHitTest(true);

                                RenderUtils::ShapePrimitive rect(RenderUtils::ShapeType::Rect);
                                rect.Offset = ImVec2(18, 46);
                                rect.Size = ImVec2(140, 90);
                                rect.Rounding = 10.0f;
                                rect.Filled = true;
                                rect.FillColor = IM_COL32(84, 128, 220, 180);
                                rect.StrokeEnabled = true;
                                rect.StrokeColor = IM_COL32(160, 210, 255, 255);
                                rect.StrokeThickness = 2.0f;

                                RenderUtils::ShapePrimitive circle(RenderUtils::ShapeType::Circle);
                                circle.Offset = ImVec2(230, 86);
                                circle.Radius = 44.0f;
                                circle.Filled = true;
                                circle.FillColor = IM_COL32(95, 210, 170, 170);
                                circle.StrokeEnabled = true;
                                circle.StrokeColor = IM_COL32(180, 255, 225, 255);
                                circle.StrokeThickness = 2.0f;

                                RenderUtils::ShapePrimitive triangle(RenderUtils::ShapeType::Triangle);
                                triangle.Offset = ImVec2(170, 46);
                                triangle.Points = { ImVec2(0, 82), ImVec2(48, 0), ImVec2(96, 82) };
                                triangle.Filled = true;
                                triangle.FillColor = IM_COL32(230, 160, 90, 180);
                                triangle.StrokeEnabled = true;
                                triangle.StrokeColor = IM_COL32(255, 220, 150, 255);
                                triangle.StrokeThickness = 2.0f;

                                RenderUtils::ShapePrimitive poly(RenderUtils::ShapeType::Polyline);
                                poly.Offset = ImVec2(12, 162);
                                poly.Points = {
                                    ImVec2(0, 0), ImVec2(40, -20), ImVec2(80, 10), ImVec2(120, -14),
                                    ImVec2(160, 18), ImVec2(200, -6), ImVec2(240, 12), ImVec2(300, -10)
                                };
                                poly.Filled = false;
                                poly.StrokeEnabled = true;
                                poly.StrokeColor = IM_COL32(255, 255, 255, 230);
                                poly.StrokeThickness = 3.0f;

                                shape.AddShape(rect).AddShape(circle).AddShape(triangle).AddShape(poly);
                                return shape;
                            }()
                        )
                )

                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("StylePanel")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent().SetPosition(ImVec2(420, 55)).SetSize(ImVec2(380, 210))
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent()
                                .SetBackgroundColor(IM_COL32(44, 38, 50, 255))
                                .SetGradient(true, IM_COL32(52, 45, 67, 255), IM_COL32(36, 32, 45, 255))
                                .SetRounding(10.0f)
                                .SetBorderColor(IM_COL32(104, 84, 130, 255))
                                .SetBorderSize(1.0f)
                                .SetOutline(true, IM_COL32(178, 134, 255, 120), 2.0f)
                                .SetContentPadding(14.0f, 14.0f)
                        )
                        .With<RenderUtils::ShadowComponent>(
                            RenderUtils::ShadowComponent()
                                .SetColor(IM_COL32(0, 0, 0, 120))
                                .SetOffset(ImVec2(4, 6))
                                .SetBlurRadius(16.0f)
                                .SetSamples(14)
                        )
                        .With<RenderUtils::GlowComponent>(
                            RenderUtils::GlowComponent(IM_COL32(198, 142, 255, 200), 12.0f, 0.75f)
                                .SetSamples(12)
                                .SetCacheEnabled(true)
                                .SetMaxSamples(16)
                        )
                        .With<RenderUtils::TextComponent>(
                            RenderUtils::TextComponent("Style + Shadow + Glow", IM_COL32(240, 230, 255, 255))
                        )
                )

                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("CustomAnimationPanel")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent().SetPosition(ImVec2(820, 55)).SetSize(ImVec2(460, 210))
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent()
                                .SetBackgroundColor(IM_COL32(40, 46, 52, 255))
                                .SetRounding(8.0f)
                                .SetBorderColor(IM_COL32(77, 88, 98, 255))
                                .SetBorderSize(1.0f)
                                .SetContentPadding(14.0f, 14.0f)
                        )
                        .With<RenderUtils::TextComponent>(
                            RenderUtils::TextComponent("Custom + Animation: click panel to toggle pulse", IM_COL32(220, 230, 240, 255))
                        )
                        .With<RenderUtils::CustomComponent>(
                            RenderUtils::CustomComponent()
                                .SetOnInput([](entt::registry& reg, entt::entity e, const RenderUtils::InputStateComponent& input) {
                                    if (!input.JustPressed || !input.IsHovered) {
                                        return;
                                    }

                                    const bool enabled = reg.any_of<RenderUtils::GlowComponent>(e);
                                    if (!enabled) {
                                        reg.emplace_or_replace<RenderUtils::GlowComponent>(e, IM_COL32(90, 220, 255, 255), 16.0f, 0.2f);
                                        RenderUtils::AnimationBuilder(reg, e)
                                            .Loop(true)
                                            .Duration(1.8f)
                                            .Ease(RenderUtils::EasingType::EaseInOutQuad)
                                            .Custom([](float t, entt::registry& r, entt::entity ent) {
                                                if (r.any_of<RenderUtils::GlowComponent>(ent)) {
                                                    auto& glow = r.get<RenderUtils::GlowComponent>(ent);
                                                    glow.Intensity = 0.25f + 0.75f * std::fabs(std::sin(t * 6.28318f));
                                                    glow.Color = IM_COL32(90, 220, 255, 220);
                                                }
                                            })
                                            .Start();
                                    } else {
                                        if (reg.any_of<RenderUtils::GlowComponent>(e)) {
                                            reg.remove<RenderUtils::GlowComponent>(e);
                                        }
                                        if (reg.any_of<RenderUtils::AnimationComponent>(e)) {
                                            reg.remove<RenderUtils::AnimationComponent>(e);
                                        }
                                    }
                                })
                                .SetOnRender([](entt::registry& reg, entt::entity e, ImDrawList* dl, ImVec2 pMin, ImVec2 pMax, bool hovered, bool) {
                                    const bool enabled = reg.any_of<RenderUtils::GlowComponent>(e);
                                    const ImU32 col = enabled ? IM_COL32(90, 220, 255, 255) : IM_COL32(140, 145, 160, 255);
                                    dl->AddRect(ImVec2(pMin.x + 14, pMin.y + 48), ImVec2(pMax.x - 14, pMax.y - 16), col, 6.0f, 0, 1.5f);
                                    if (hovered) {
                                        dl->AddText(ImVec2(pMin.x + 20, pMax.y - 34), IM_COL32(255, 255, 255, 230), enabled ? "Active" : "Hover + Click");
                                    }
                                })
                        )
                )

                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("StaticImagePanel")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent().SetPosition(ImVec2(20, 290)).SetSize(ImVec2(250, 240))
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent()
                                .SetBackgroundColor(IM_COL32(36, 40, 46, 255))
                                .SetRounding(8.0f)
                                .SetBorderColor(IM_COL32(68, 72, 82, 255))
                                .SetBorderSize(1.0f)
                        )
                        .With<Components::ImageLoader>(
                            Components::ImageLoader()
                                .AddUrl("https://upload.wikimedia.org/wikipedia/commons/3/3f/Fronalpstock_big.jpg")
                        )
                )

                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("MultiImagePanel")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent().SetPosition(ImVec2(290, 290)).SetSize(ImVec2(250, 240))
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent()
                                .SetBackgroundColor(IM_COL32(36, 40, 46, 255))
                                .SetRounding(8.0f)
                                .SetBorderColor(IM_COL32(68, 72, 82, 255))
                                .SetBorderSize(1.0f)
                        )
                        .With<Components::ImageLoader>(
                            []() {
                                Components::ImageLoader loader;
                                loader.AddUrl("https://upload.wikimedia.org/wikipedia/commons/thumb/a/a9/Example.jpg/640px-Example.jpg");
                                loader.AddUrl("https://upload.wikimedia.org/wikipedia/commons/9/9a/Gull_portrait_ca_usa.jpg");
                                loader.AddPath("assets/local_preview.png");
                                loader.SetActiveSource(0);
                                return loader;
                            }()
                        )
                        .With<RenderUtils::CustomComponent>(
                            RenderUtils::CustomComponent().SetOnInput([](entt::registry& reg, entt::entity e, const RenderUtils::InputStateComponent& input) {
                                if (!input.JustPressed || !input.IsHovered || !reg.any_of<Components::ImageLoader>(e)) {
                                    return;
                                }
                                auto& loader = reg.get<Components::ImageLoader>(e);
                                const int count = static_cast<int>(loader.Sources.size());
                                if (count <= 0) {
                                    return;
                                }
                                loader.ActiveSourceIndex = (loader.ActiveSourceIndex + 1) % count;
                            })
                        )
                )

                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("GifPanel")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent().SetPosition(ImVec2(560, 290)).SetSize(ImVec2(320, 240))
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent()
                                .SetBackgroundColor(IM_COL32(36, 40, 46, 255))
                                .SetRounding(8.0f)
                                .SetBorderColor(IM_COL32(68, 72, 82, 255))
                                .SetBorderSize(1.0f)
                        )
                        .With<Components::ImageLoader>(
                            Components::ImageLoader()
                                .AddUrl("https://media.giphy.com/media/ICOgUNjpvO0PC/giphy.gif")
                                .SetPlayback(true, true, false, 1.0f)
                        )
                )

                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("CollisionPanel")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent().SetPosition(ImVec2(900, 290)).SetSize(ImVec2(380, 240))
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent()
                                .SetBackgroundColor(IM_COL32(36, 40, 46, 255))
                                .SetRounding(8.0f)
                                .SetBorderColor(IM_COL32(68, 72, 82, 255))
                                .SetBorderSize(1.0f)
                                .SetContentPadding(10.0f, 10.0f)
                        )
                        .With<RenderUtils::TextComponent>(
                            RenderUtils::TextComponent("Collision + Drag (drag boxes in debug mode)", IM_COL32(215, 225, 240, 255))
                        )
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Panel>("CollisionBoxA")
                                .With<RenderUtils::TransformComponent>(
                                    RenderUtils::TransformComponent().SetPosition(ImVec2(20, 90)).SetSize(ImVec2(140, 90))
                                )
                                .With<RenderUtils::StyleComponent>(
                                    RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(108, 170, 255, 200)).SetRounding(6.0f)
                                )
                                .With<RenderUtils::DraggableComponent>(
                                    RenderUtils::DraggableComponent().SetMode(RenderUtils::DragMode::Free).SetConstraint(RenderUtils::DragConstraint::Parent)
                                )
                                .With<RenderUtils::CollisionComponent>(RenderUtils::CollisionComponent(true))
                                .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Box A", IM_COL32(255, 255, 255, 255)))
                        )
                        .Child(
                            RenderUtils::UIBuilder::Begin(g_registry)
                                .Create<RenderUtils::ContainerType::Panel>("CollisionBoxB")
                                .With<RenderUtils::TransformComponent>(
                                    RenderUtils::TransformComponent().SetPosition(ImVec2(190, 120)).SetSize(ImVec2(140, 90))
                                )
                                .With<RenderUtils::StyleComponent>(
                                    RenderUtils::StyleComponent().SetBackgroundColor(IM_COL32(255, 154, 112, 200)).SetRounding(6.0f)
                                )
                                .With<RenderUtils::DraggableComponent>(
                                    RenderUtils::DraggableComponent().SetMode(RenderUtils::DragMode::Free).SetConstraint(RenderUtils::DragConstraint::Parent)
                                )
                                .With<RenderUtils::CollisionComponent>(RenderUtils::CollisionComponent(true))
                                .With<RenderUtils::TextComponent>(RenderUtils::TextComponent("Box B", IM_COL32(255, 255, 255, 255)))
                        )
                )
            .End()

            .Create<RenderUtils::ContainerType::Window>("HeaderlessPreview")
                .With<RenderUtils::TransformComponent>(
                    RenderUtils::TransformComponent().SetPosition(ImVec2(1380, 120)).SetSize(ImVec2(190, 220))
                )
                .With<RenderUtils::StyleComponent>(
                    RenderUtils::StyleComponent()
                        .SetBackgroundColor(IM_COL32(44, 48, 58, 255))
                        .SetRounding(8.0f)
                        .SetBorderColor(IM_COL32(88, 96, 110, 255))
                        .SetBorderSize(1.0f)
                        .SetLayer(RenderUtils::ZOrder::Top)
                )
                .With<RenderUtils::WindowHeaderComponent>(RenderUtils::WindowHeaderComponent().SetEnabled(false))
                .With<RenderUtils::DraggableComponent>(RenderUtils::DraggableComponent().SetMode(RenderUtils::DragMode::Free))
                .With<RenderUtils::TextComponent>(
                    RenderUtils::TextComponent("Header disabled via WindowHeaderComponent", IM_COL32(230, 235, 245, 255))
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
            // We must open the command list BEFORE UIRenderer::Update because ImageLoaderSystem may record upload commands.
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

            // Update Library Systems
            RenderUtils::UIRenderer::Update(g_registry, 1.0f / io.Framerate);

            // Render Custom UI
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
        Components::ImageLoaderSystem::Shutdown(g_registry);
        ImguiRender::Cleanup();
        DX12Init::CleanupDeviceD3D();
        DestroyWindow(hWnd);
        UnregisterClass(wc.lpszClassName, wc.hInstance);

        return 0;
    }
}

