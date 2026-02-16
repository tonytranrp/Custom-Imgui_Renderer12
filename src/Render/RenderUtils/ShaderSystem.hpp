#pragma once

#include <cstddef>
#include <d3d12.h>
#include <functional>
#include <string>
#include <vector>

#include "Components/ShaderComponent.hpp"
#include "entt/entt.hpp"
#include "imgui.h"

namespace RenderUtils::ShaderSystem {
    using EmbeddedShaderProvider = bool(*)(std::string& outText);
    using ShaderUniformResolver = std::function<bool(
        const ShaderAutoUniformContext& context,
        ShaderParamType expectedType,
        ShaderParamValue& outValue,
        std::string& outError)>;

    void Initialize(ID3D12Device* device);
    void Shutdown();

    // File-key alias registry for file-backed source indirection.
    void RegisterSourceAlias(const std::string& key, const std::string& path);
    void UnregisterSourceAlias(const std::string& key);
    void ResetDefaultSourceAliases();

    // Embedded source providers (compile-time embedded text assets).
    void RegisterEmbeddedSource(const std::string& key, EmbeddedShaderProvider provider);
    void UnregisterEmbeddedSource(const std::string& key);
    void ResetDefaultEmbeddedSources();

    // User uniform resolvers for shader parameters bound by string key.
    void RegisterUniformResolver(const std::string& key, ShaderUniformResolver resolver);
    void UnregisterUniformResolver(const std::string& key);
    void ResetDefaultUniformResolvers();
    void QueryUniformResolverKeys(std::vector<std::string>& outKeys);

    // Callback-driven pass context (for inline ImGui callback rendering).
    void BeginImGuiPass(
        ID3D12GraphicsCommandList* cmd,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        const ImVec2& displaySize);

    void EndImGuiPass();

    void QueueEntity(
        entt::registry& registry,
        entt::entity entity,
        const ImVec2& pMin,
        const ImVec2& pMax,
        int zOrder,
        float alpha,
        bool forGlow,
        bool forShadow);

    bool TryQueueEntity(
        entt::registry& registry,
        entt::entity entity,
        const ImVec2& pMin,
        const ImVec2& pMax,
        int zOrder,
        float alpha,
        bool forGlow,
        bool forShadow);

    bool TryQueueEntityImGui(
        entt::registry& registry,
        entt::entity entity,
        ImDrawList* drawList,
        const ImVec2& objectMin,
        const ImVec2& objectMax,
        int zOrder,
        float alpha,
        bool forGlow,
        bool forShadow);

    void UpdateCompile(entt::registry& registry);

    void RenderQueued(
        ID3D12GraphicsCommandList* cmd,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        const ImVec2& displaySize);

    void ClearQueue();

    // Debug/status helpers
    size_t QueryQueuedCount();
    size_t QueryImGuiCallbackQueuedCount();
    size_t QueryImGuiCallbackRuntimeFailureCount();
}
