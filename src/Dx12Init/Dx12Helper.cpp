#include "Dx12Init.hpp"

namespace DX12Init {
    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuSrvHandle(int index) {
        auto handle = g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuSrvHandle(int index) {
        auto handle = g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += index * g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return handle;
    }
}
