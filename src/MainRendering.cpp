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

        RenderUtils::UIRenderer::Init();

        // Retrieve Rust Strings safely
        static std::string rustGreetingStr;
        char* rawGreeting = get_rust_greeting();
        if (rawGreeting) {
            rustGreetingStr = rawGreeting;
            free_rust_string(rawGreeting);
        } else {
            rustGreetingStr = "Rust Greeting Failed";
        }
        
        static std::string rustEmbeddedStr;
        size_t embedLen = 0;
        const uint8_t* embedData = get_embedded_data(&embedLen);
        if (embedData && embedLen > 0) {
            rustEmbeddedStr = std::string(reinterpret_cast<const char*>(embedData), embedLen);
        } else {
            rustEmbeddedStr = "Failed to load embedded data.";
        }

        // --- Use UIBuilder for Cleaner Scene Creation ---

        // 3. Child Text Entity (Attached to Window)
        const char* longText = "This is a custom rendered window.\n"
                               "It features a header bar, a background color, and text content.\n"
                               "The text you are reading right now is configured to WRAP when it reaches the edge of the window, "
                               "and it will be CLIPPED if it exceeds the vertical bounds of the container.\n\n"
                               "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
                               "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.";

        RenderUtils::UIBuilder::Begin(g_registry)
            // 1. Full Screen Background (Locked, ZIndex -1)
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

            // 2. Rich Text Window
            .Create<RenderUtils::ContainerType::Window>("Rich Text Demo")
                .With<RenderUtils::TransformComponent>(
                    RenderUtils::TransformComponent()
                        .SetPosition(ImVec2(100, 100))
                        .SetSize(ImVec2(700, 600))
                        .SetScale(1.0f)
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
                    RenderUtils::DraggableComponent()
                        .SetMode(RenderUtils::DragMode::Free)
                )
                .With<RenderUtils::ClipComponent>(
                    RenderUtils::ClipComponent().SetClipChildren(true)
                )

                // Header / Title (Centered)
                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Label>("TitleLabel")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent()
                                .SetPosition(ImVec2(0, 30))
                                .SetSize(ImVec2(680, 40))
                        )
                        .With<RenderUtils::TextComponent>(
                            RenderUtils::TextComponent("Rich Text Features", IM_COL32(255, 255, 255, 255))
                                .SetFlags(RenderUtils::TextFlags::PrecisionMode)
                                .Align(RenderUtils::TextAlign::Center)
                                .Span(" (Beta)", IM_COL32(255, 200, 100, 255), RenderUtils::TextStyle::Bold)
                        )
                        .With<RenderUtils::DraggableComponent>(
                            RenderUtils::DraggableComponent()
                                .SetConstraint(RenderUtils::DragConstraint::Parent)
                                .SetMode(RenderUtils::DragMode::Free) // Make draggable within window
                        )
                )

                // Paragraph with formatting
                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Label>("Paragraph")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent()
                                .SetPosition(ImVec2(20, 80))
                                .SetSize(ImVec2(660, 150))
                        )
                        .With<RenderUtils::TextComponent>(
                            RenderUtils::TextComponent("This is a standard text block. ", IM_COL32(200, 200, 200, 255))
                                .SetFlags(RenderUtils::TextFlags::Wrap | RenderUtils::TextFlags::Clip | RenderUtils::TextFlags::PrecisionMode)
                                .Align(RenderUtils::TextAlign::Left)
                                .Span("However, we can now add ", IM_COL32(200, 200, 200, 255))
                                .Span("BOLD TEXT", IM_COL32(255, 100, 100, 255), RenderUtils::TextStyle::Bold)
                                .Span(" and ", IM_COL32(200, 200, 200, 255))
                                .Span("colored text", IM_COL32(100, 255, 100, 255))
                                .Span(" inline! The system supports automatic wrapping of these mixed spans. ", IM_COL32(200, 200, 200, 255))
                                .Span("This behaves similarly to a word processor.", IM_COL32(100, 200, 255, 255), RenderUtils::TextStyle::Bold)
                        )
                        .With<RenderUtils::DraggableComponent>(
                            RenderUtils::DraggableComponent()
                                .SetConstraint(RenderUtils::DragConstraint::Parent)
                                .SetMode(RenderUtils::DragMode::Free) // Make draggable within window
                        )
                )

                // Scrollable Area Example
                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("ScrollArea")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent()
                                .SetPosition(ImVec2(20, 240))
                                .SetSize(ImVec2(660, 150))
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent()
                                .SetBackgroundColor(IM_COL32(20, 20, 20, 255))
                                .SetBorderColor(IM_COL32(60, 60, 60, 255))
                                .SetBorderSize(1.0f)
                                .SetRounding(5.0f)
                        )
                        .With<RenderUtils::ClipComponent>(
                            RenderUtils::ClipComponent().SetClipChildren(true)
                        )
                        .With<RenderUtils::ScrollComponent>(
                            RenderUtils::ScrollComponent()
                                .SetContentHeight(600.0f) // Larger than view height
                                .SetShowScrollbar(true)
                        )
                        .With<RenderUtils::TextComponent>(
                            RenderUtils::TextComponent("Scrollable Content:\n", IM_COL32(150, 150, 255, 255))
                                .SetFlags(RenderUtils::TextFlags::Wrap)
                                .Span(longText, IM_COL32(180, 180, 180, 255))
                                .Span("\n\nExtra content to force scrolling...\n", IM_COL32(100, 255, 100, 255))
                                .Span(longText, IM_COL32(150, 150, 150, 255))
                        )
                )

                // Slider Example
                .Child(
                     RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("SliderContainer")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent()
                                .SetPosition(ImVec2(20, 400))
                                .SetSize(ImVec2(300, 30))
                        )
                        .With<RenderUtils::SliderComponent>(
                            RenderUtils::SliderComponent()
                                .SetMin(0.0f)
                                .SetMax(100.0f)
                                .SetValue(50.0f)
                        )
                        .With<RenderUtils::TextComponent>(
                            RenderUtils::TextComponent("Volume: ", IM_COL32(255, 255, 255, 255))
                                .SetFlags(RenderUtils::TextFlags::PrecisionMode)
                        )
                )

                // Input Field Example
                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Panel>("InputBox")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent()
                                .SetPosition(ImVec2(20, 450))
                                .SetSize(ImVec2(300, 40))
                        )
                        .With<RenderUtils::StyleComponent>(
                            RenderUtils::StyleComponent()
                                .SetBackgroundColor(IM_COL32(10, 10, 10, 255))
                                .SetBorderColor(IM_COL32(100, 100, 100, 255))
                                .SetBorderSize(1.0f)
                                .SetRounding(4.0f)
                        )
                        .With<RenderUtils::TextInputComponent>(
                            RenderUtils::TextInputComponent()
                                .SetPlaceholder("Type here...")
                                .SetMaxLength(50)
                        )
                        .With<RenderUtils::InputStateComponent>(
                            RenderUtils::InputStateComponent().SetBlockInput(true)
                        )
                )

                // Right Aligned Footer
                .Child(
                    RenderUtils::UIBuilder::Begin(g_registry)
                        .Create<RenderUtils::ContainerType::Label>("Footer")
                        .With<RenderUtils::TransformComponent>(
                            RenderUtils::TransformComponent()
                                .SetPosition(ImVec2(20, 550))
                                .SetSize(ImVec2(660, 30))
                        )
                        .With<RenderUtils::TextComponent>(
                            RenderUtils::TextComponent("Signed: ", IM_COL32(150, 150, 150, 255))
                                .Align(RenderUtils::TextAlign::Right)
                                .Span("Tony", IM_COL32(255, 255, 255, 255), RenderUtils::TextStyle::Bold)
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

            // Resolve Z-Order Dependencies
            RenderUtils::UIRenderer::ResolveDepth(g_registry);

            // Update Input (Dragging, Hovering, Parenting)
            RenderUtils::UIRenderer::UpdateInput(g_registry);

            // Render Custom UI
            RenderUtils::UIRenderer::Render(g_registry);

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

            // Rendering
            ImGui::Render();

            FrameContext* frameCtx = DX12Init::WaitForNextFrameResources();
            UINT backBufferIdx = DX12Init::g_pSwapChain->GetCurrentBackBufferIndex();
            frameCtx->CommandAllocator->Reset();

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource   = DX12Init::g_mainRenderTargetResource[backBufferIdx];
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

            DX12Init::g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
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
