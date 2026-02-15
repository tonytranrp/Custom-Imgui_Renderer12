#include "ImguiRender.hpp"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

namespace ImguiRender {
    void Init(HWND hWnd, ID3D12Device* device, int num_frames, DXGI_FORMAT rtv_format, ID3D12DescriptorHeap* srv_heap, ID3D12CommandQueue* command_queue) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(hWnd);
        
        ImGui_ImplDX12_InitInfo init_info = {};
        init_info.Device = device;
        init_info.CommandQueue = command_queue;
        init_info.NumFramesInFlight = num_frames;
        init_info.RTVFormat = rtv_format;
        init_info.SrvDescriptorHeap = srv_heap;
        init_info.LegacySingleSrvCpuDescriptor = srv_heap->GetCPUDescriptorHandleForHeapStart();
        init_info.LegacySingleSrvGpuDescriptor = srv_heap->GetGPUDescriptorHandleForHeapStart();
        
        ImGui_ImplDX12_Init(&init_info);
    }

    void NewFrame() {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }
    
    void RenderDrawData(ID3D12GraphicsCommandList* command_list) {
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);
    }

    void Cleanup() {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}
