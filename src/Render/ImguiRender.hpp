#pragma once

#include "imgui.h"
#include <d3d12.h>
#include <dxgi1_4.h>

namespace ImguiRender {
    void Init(HWND hWnd, ID3D12Device* device, int num_frames, DXGI_FORMAT rtv_format, ID3D12DescriptorHeap* srv_heap, ID3D12CommandQueue* command_queue);
    void NewFrame();
    void RenderDrawData(ID3D12GraphicsCommandList* command_list);
    void Cleanup();
}
