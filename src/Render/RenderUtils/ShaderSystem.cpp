#include "ShaderSystem.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <d3dcompiler.h>
#include <wrl/client.h>

#include "Components/GlowComponent.hpp"
#include "Components/ShaderComponent.hpp"
#include "Components/ShadowComponent.hpp"
#include "Components/StyleComponent.hpp"
#include "Dx12Init/Dx12Init.hpp"

#if __has_include(<dxcapi.h>)
#define RENDERUTILS_HAS_DXC 1
#include <dxcapi.h>
#else
#define RENDERUTILS_HAS_DXC 0
#endif

namespace RenderUtils::ShaderSystem {
    using Microsoft::WRL::ComPtr;

    namespace {
        enum class BlendMode {
            Alpha,
            Additive
        };

        struct ShaderGlobals {
            float RectMinMax[4] = {};
            float DrawMinMax[4] = {};
            float Color[4] = {};
            float Params0[4] = {};
            float Params1[4] = {};
            float Params2[4] = {};
            float Params3[4] = {};
        };

        struct QueuedDraw {
            entt::entity Entity = entt::null;
            int ZOrder = 0;
            uint64_t EntityId = 0;
            uint64_t ProgramHandle = 0;
            ShaderGlobals Globals;
            std::vector<uint8_t> UserBytes;
        };

        struct DeferredRelease {
            UINT64 FenceValue = 0;
            ComPtr<ID3D12Resource> Resource;
        };

        struct CompiledProgram {
            uint64_t Handle = 0;
            ShaderBackendMode BackendUsed = ShaderBackendMode::D3DCompileOnly;
            BlendMode Blend = BlendMode::Alpha;
            std::string CacheKey;
            std::string LastError;
            ComPtr<ID3D12PipelineState> Pso;
        };

        struct ImGuiPassContext {
            ID3D12GraphicsCommandList* CommandList = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE RTV{};
            ImVec2 DisplaySize = ImVec2(1.0f, 1.0f);
            bool Active = false;
            size_t CallbackQueuedCount = 0;
            size_t CallbackRuntimeFailureCount = 0;
        };

        ID3D12Device* s_Device = nullptr;
        ComPtr<ID3D12RootSignature> s_RootSignature;
        std::unordered_map<std::string, uint64_t> s_ProgramByKey;
        std::unordered_map<uint64_t, CompiledProgram> s_Programs;
        std::unordered_map<std::string, std::filesystem::file_time_type> s_FileWriteTimes;
        std::vector<QueuedDraw> s_QueuedDraws;
        std::vector<std::unique_ptr<QueuedDraw>> s_ImGuiCallbackDraws;
        ImGuiPassContext s_ImGuiPass;
        size_t s_LastImGuiCallbackQueuedCount = 0;
        size_t s_LastImGuiCallbackRuntimeFailureCount = 0;
        std::vector<DeferredRelease> s_DeferredReleases;
        uint64_t s_NextProgramHandle = 1;
        constexpr size_t kMaxQueuedDrawsPerFrame = 4096u;
        constexpr const char* kSharedHeaderPath = "assets/shaders/ui_shader_shared.hlsli";
        constexpr const char* kDefaultVertexPath = "assets/shaders/ui_default_vs.hlsl";
        constexpr const char* kDefaultGlowPath = "assets/shaders/glow_default.hlsl";
        constexpr const char* kDefaultShadowPath = "assets/shaders/shadow_default.hlsl";
        constexpr const char* kInlineModeDisabledError = "Inline mode disabled by file-only policy.";
        std::unordered_map<std::string, std::string> s_SourceAliases;

        bool ReadTextFile(const std::string& path, std::string& outText) {
            std::ifstream file(path, std::ios::in | std::ios::binary);
            if (!file.is_open()) {
                return false;
            }
            std::ostringstream oss;
            oss << file.rdbuf();
            outText = oss.str();
            return true;
        }

        std::string ToString(ShaderSourceMode mode) {
            switch (mode) {
            case ShaderSourceMode::EmbeddedCpp: return "EmbeddedCpp";
            case ShaderSourceMode::EmbeddedRust: return "EmbeddedRust";
            case ShaderSourceMode::File: return "File";
            case ShaderSourceMode::Inline: return "Inline";
            }
            return "Unknown";
        }

        bool LooksLikeFilePath(const std::string& value) {
            if (value.empty()) {
                return false;
            }
            if (value.find('/') != std::string::npos || value.find('\\') != std::string::npos) {
                return true;
            }
            if (value.find(':') != std::string::npos) {
                return true;
            }
            const std::filesystem::path pathValue(value);
            if (!pathValue.has_extension()) {
                return false;
            }
            std::string ext = pathValue.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return ext == ".hlsl" || ext == ".hlsli";
        }

        namespace SourceAliasRegistry {
            void SeedDefaults() {
                s_SourceAliases["shader.shared"] = kSharedHeaderPath;
                s_SourceAliases["shader.default.vs"] = kDefaultVertexPath;
                s_SourceAliases["glow.default"] = kDefaultGlowPath;
                s_SourceAliases["shadow.default"] = kDefaultShadowPath;
            }

            bool TryResolve(const std::string& key, std::string& outPath, std::string& outError) {
                outPath.clear();
                outError.clear();
                if (key.empty()) {
                    outError = "Alias key is empty.";
                    return false;
                }
                const auto it = s_SourceAliases.find(key);
                if (it == s_SourceAliases.end()) {
                    outError = "Alias key not found: " + key;
                    return false;
                }
                if (it->second.empty()) {
                    outError = "Alias key has empty file path: " + key;
                    return false;
                }
                outPath = it->second;
                return true;
            }
        } // namespace SourceAliasRegistry

        namespace SourceResolver {
            bool ResolvePath(const ShaderSourceSpec& source, std::string& outPath, std::string& outError) {
                outPath.clear();
                outError.clear();
                switch (source.Mode) {
                case ShaderSourceMode::File:
                    if (source.KeyOrPathOrInline.empty()) {
                        outError = "File mode source path is empty.";
                        return false;
                    }
                    outPath = source.KeyOrPathOrInline;
                    return true;
                case ShaderSourceMode::EmbeddedCpp:
                case ShaderSourceMode::EmbeddedRust:
                    if (!SourceAliasRegistry::TryResolve(source.KeyOrPathOrInline, outPath, outError)) {
                        outError = ToString(source.Mode) + ": " + outError;
                        return false;
                    }
                    return true;
                case ShaderSourceMode::Inline:
                    outError = kInlineModeDisabledError;
                    return false;
                }
                outError = "Unsupported ShaderSourceMode.";
                return false;
            }

            bool ResolveText(const ShaderSourceSpec& source, std::string& outText, std::string& outError) {
                outText.clear();
                std::string path;
                if (!ResolvePath(source, path, outError)) {
                    return false;
                }
                if (!ReadTextFile(path, outText)) {
                    outError = "Failed to read shader file: " + path;
                    return false;
                }
                return true;
            }

            bool ResolveSharedPrelude(std::string& outText, std::string& outError) {
                outText.clear();
                ShaderSourceSpec shared;
                shared.Mode = ShaderSourceMode::EmbeddedCpp;
                shared.KeyOrPathOrInline = "shader.shared";
                shared.EntryPoint = "main";
                shared.TargetProfile = "ps_5_0";
                return ResolveText(shared, outText, outError);
            }
        } // namespace SourceResolver

        namespace CompileHelpers {
        std::wstring Utf8ToWide(const std::string& text) {
            if (text.empty()) {
                return {};
            }
            const int required = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
            if (required <= 0) {
                return {};
            }
            std::wstring out;
            out.resize(static_cast<size_t>(required));
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), required);
            return out;
        }

        bool CompileWithD3DCompile(
            const std::string& source,
            const std::string& entry,
            const std::string& target,
            std::vector<uint8_t>& outBytecode,
            std::string& outError) {
            outBytecode.clear();
            outError.clear();

            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

            ComPtr<ID3DBlob> shaderBlob;
            ComPtr<ID3DBlob> errorBlob;
            const HRESULT hr = D3DCompile(
                source.data(),
                source.size(),
                nullptr,
                nullptr,
                nullptr,
                entry.c_str(),
                target.c_str(),
                flags,
                0,
                &shaderBlob,
                &errorBlob);

            if (FAILED(hr) || !shaderBlob) {
                if (errorBlob && errorBlob->GetBufferPointer()) {
                    outError.assign(
                        static_cast<const char*>(errorBlob->GetBufferPointer()),
                        errorBlob->GetBufferSize());
                } else {
                    outError = "D3DCompile failed.";
                }
                return false;
            }

            outBytecode.resize(shaderBlob->GetBufferSize());
            std::memcpy(outBytecode.data(), shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
            return true;
        }

#if RENDERUTILS_HAS_DXC
        bool CompileWithDXC(
            const std::string& source,
            const std::string& entry,
            const std::string& target,
            std::vector<uint8_t>& outBytecode,
            std::string& outError) {
            outBytecode.clear();
            outError.clear();

            HMODULE module = LoadLibraryA("dxcompiler.dll");
            if (!module) {
                outError = "dxcompiler.dll not found at runtime.";
                return false;
            }
            struct ModuleGuard {
                HMODULE Handle = nullptr;
                ~ModuleGuard() {
                    if (Handle) {
                        FreeLibrary(Handle);
                    }
                }
            } guard{ module };

            using DxcCreateInstanceProc = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
            const auto createInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(module, "DxcCreateInstance"));
            if (!createInstance) {
                outError = "DxcCreateInstance export not found.";
                return false;
            }

            ComPtr<IDxcUtils> utils;
            ComPtr<IDxcCompiler3> compiler;
            if (FAILED(createInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) || !utils) {
                outError = "Failed to create IDxcUtils.";
                return false;
            }
            if (FAILED(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))) || !compiler) {
                outError = "Failed to create IDxcCompiler3.";
                return false;
            }

            const std::wstring entryW = Utf8ToWide(entry);
            const std::wstring targetW = Utf8ToWide(target);
            if (entryW.empty() || targetW.empty()) {
                outError = "Failed UTF-8 to UTF-16 conversion for DXC arguments.";
                return false;
            }

            DxcBuffer sourceBuffer{};
            sourceBuffer.Ptr = source.data();
            sourceBuffer.Size = source.size();
            sourceBuffer.Encoding = DXC_CP_UTF8;

            std::vector<LPCWSTR> args;
            args.push_back(L"-E");
            args.push_back(entryW.c_str());
            args.push_back(L"-T");
            args.push_back(targetW.c_str());
            args.push_back(L"-Zpr");
#if defined(_DEBUG)
            args.push_back(L"-Od");
            args.push_back(L"-Zi");
#else
            args.push_back(L"-O3");
#endif

            ComPtr<IDxcResult> compileResult;
            const HRESULT hr = compiler->Compile(&sourceBuffer, args.data(), static_cast<UINT32>(args.size()), nullptr, IID_PPV_ARGS(&compileResult));
            if (FAILED(hr) || !compileResult) {
                outError = "IDxcCompiler3::Compile failed.";
                return false;
            }

            HRESULT status = E_FAIL;
            compileResult->GetStatus(&status);
            ComPtr<IDxcBlobUtf8> errors;
            if (SUCCEEDED(compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) && errors && errors->GetStringLength() > 0) {
                outError = errors->GetStringPointer();
            }

            if (FAILED(status)) {
                if (outError.empty()) {
                    outError = "DXC compile failed.";
                }
                return false;
            }

            ComPtr<IDxcBlob> objectBlob;
            if (FAILED(compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&objectBlob), nullptr)) || !objectBlob) {
                outError = "DXC output blob unavailable.";
                return false;
            }

            outBytecode.resize(objectBlob->GetBufferSize());
            std::memcpy(outBytecode.data(), objectBlob->GetBufferPointer(), objectBlob->GetBufferSize());
            return true;
        }
#endif
        bool CompileShaderBytecode(
            const std::string& source,
            const std::string& entry,
            const std::string& target,
            ShaderBackendMode backendMode,
            std::vector<uint8_t>& outBytecode,
            ShaderBackendMode& outBackendUsed,
            std::string& outError) {
            outBytecode.clear();
            outError.clear();

            if (backendMode == ShaderBackendMode::D3DCompileOnly) {
                outBackendUsed = ShaderBackendMode::D3DCompileOnly;
                return CompileWithD3DCompile(source, entry, target, outBytecode, outError);
            }

            if (backendMode == ShaderBackendMode::DXCOnly) {
#if RENDERUTILS_HAS_DXC
                outBackendUsed = ShaderBackendMode::DXCOnly;
                return CompileWithDXC(source, entry, target, outBytecode, outError);
#else
                outBackendUsed = ShaderBackendMode::DXCOnly;
                outError = "DXCOnly selected, but dxcapi.h is not available in this toolchain.";
                return false;
#endif
            }

#if RENDERUTILS_HAS_DXC
            if (CompileWithDXC(source, entry, target, outBytecode, outError)) {
                outBackendUsed = ShaderBackendMode::DXCOnly;
                return true;
            }
            std::string dxcError = outError;
            if (CompileWithD3DCompile(source, entry, target, outBytecode, outError)) {
                outBackendUsed = ShaderBackendMode::D3DCompileOnly;
                return true;
            }
            outError = "DXC failed: " + dxcError + "\nD3DCompile fallback failed: " + outError;
            return false;
#else
            outBackendUsed = ShaderBackendMode::D3DCompileOnly;
            return CompileWithD3DCompile(source, entry, target, outBytecode, outError);
#endif
        }

        bool EnsureRootSignature(std::string& outError) {
            outError.clear();
            if (s_RootSignature) {
                return true;
            }
            if (!s_Device) {
                outError = "ShaderSystem not initialized (device is null).";
                return false;
            }

            D3D12_ROOT_PARAMETER rootParams[2] = {};
            rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            rootParams[0].Descriptor.ShaderRegister = 0;
            rootParams[0].Descriptor.RegisterSpace = 0;
            rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            rootParams[1].Descriptor.ShaderRegister = 1;
            rootParams[1].Descriptor.RegisterSpace = 0;
            rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_ROOT_SIGNATURE_DESC desc = {};
            desc.NumParameters = 2;
            desc.pParameters = rootParams;
            desc.NumStaticSamplers = 0;
            desc.pStaticSamplers = nullptr;
            desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> signatureBlob;
            ComPtr<ID3DBlob> errorBlob;
            const HRESULT serializeHr = D3D12SerializeRootSignature(
                &desc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                &signatureBlob,
                &errorBlob);
            if (FAILED(serializeHr) || !signatureBlob) {
                if (errorBlob && errorBlob->GetBufferPointer()) {
                    outError.assign(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
                } else {
                    outError = "Failed to serialize root signature.";
                }
                return false;
            }

            const HRESULT createHr = s_Device->CreateRootSignature(
                0,
                signatureBlob->GetBufferPointer(),
                signatureBlob->GetBufferSize(),
                IID_PPV_ARGS(&s_RootSignature));
            if (FAILED(createHr) || !s_RootSignature) {
                outError = "Failed to create root signature.";
                return false;
            }

            return true;
        }

        std::string BuildShaderCacheKey(
            BlendMode blend,
            ShaderStageMode stageMode,
            const ShaderSourceSpec& vsSource,
            const ShaderSourceSpec& psSource,
            const std::string& userParamSignature,
            bool forGlow,
            bool forShadow,
            const std::string& vsText,
            const std::string& psText) {
            std::string key;
            key.reserve(vsText.size() + psText.size() + 256);
            key += std::to_string(static_cast<int>(blend));
            key += "|";
            key += std::to_string(static_cast<int>(stageMode));
            key += "|";
            key += std::to_string(static_cast<int>(vsSource.Mode));
            key += "|";
            key += vsSource.EntryPoint;
            key += "|";
            key += vsSource.TargetProfile;
            key += "|";
            key += std::to_string(static_cast<int>(psSource.Mode));
            key += "|";
            key += psSource.EntryPoint;
            key += "|";
            key += psSource.TargetProfile;
            key += "|";
            key += forGlow ? "1" : "0";
            key += forShadow ? "1" : "0";
            key += "|";
            key += userParamSignature;
            key += "|VS|";
            key += vsText;
            key += "|PS|";
            key += psText;
            return key;
        }

        std::string BuildUserParamSignature(const std::vector<ShaderParameter>& params) {
            std::string signature;
            for (const auto& param : params) {
                signature += param.Name;
                signature += ":";
                signature += std::to_string(static_cast<int>(param.Type));
                signature += ";";
            }
            return signature;
        }

        void MarkShaderFailure(ShaderComponent* shader, const std::string& error) {
            if (!shader) {
                return;
            }
            shader->IsCompiled = false;
            shader->ProgramHandle = 0;
            shader->LastError = error;
        }

        bool CreatePipelineState(
            const std::vector<uint8_t>& vsBytes,
            const std::vector<uint8_t>& psBytes,
            BlendMode blend,
            ComPtr<ID3D12PipelineState>& outPso,
            std::string& outError) {
            outPso.Reset();
            outError.clear();

            if (!s_Device || !s_RootSignature) {
                outError = "Device or root signature is not initialized.";
                return false;
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
            desc.pRootSignature = s_RootSignature.Get();
            desc.VS = { vsBytes.data(), static_cast<UINT>(vsBytes.size()) };
            desc.PS = { psBytes.data(), static_cast<UINT>(psBytes.size()) };
            desc.BlendState.AlphaToCoverageEnable = FALSE;
            desc.BlendState.IndependentBlendEnable = FALSE;
            for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
                auto& rt = desc.BlendState.RenderTarget[i];
                rt.BlendEnable = FALSE;
                rt.LogicOpEnable = FALSE;
                rt.SrcBlend = D3D12_BLEND_ONE;
                rt.DestBlend = D3D12_BLEND_ZERO;
                rt.BlendOp = D3D12_BLEND_OP_ADD;
                rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                rt.DestBlendAlpha = D3D12_BLEND_ZERO;
                rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                rt.LogicOp = D3D12_LOGIC_OP_NOOP;
                rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            }
            if (blend == BlendMode::Additive) {
                auto& rt = desc.BlendState.RenderTarget[0];
                rt.BlendEnable = TRUE;
                rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                rt.DestBlend = D3D12_BLEND_ONE;
                rt.BlendOp = D3D12_BLEND_OP_ADD;
                rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                rt.DestBlendAlpha = D3D12_BLEND_ONE;
                rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            } else {
                auto& rt = desc.BlendState.RenderTarget[0];
                rt.BlendEnable = TRUE;
                rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                rt.BlendOp = D3D12_BLEND_OP_ADD;
                rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            }

            desc.SampleMask = UINT_MAX;
            desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            desc.RasterizerState.FrontCounterClockwise = FALSE;
            desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            desc.RasterizerState.DepthClipEnable = TRUE;
            desc.RasterizerState.MultisampleEnable = FALSE;
            desc.RasterizerState.AntialiasedLineEnable = FALSE;
            desc.RasterizerState.ForcedSampleCount = 0;
            desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.StencilEnable = FALSE;
            desc.InputLayout = { nullptr, 0 };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;

            const HRESULT hr = s_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&outPso));
            if (FAILED(hr) || !outPso) {
                outError = "Failed to create graphics pipeline state.";
                return false;
            }
            return true;
        }
        } // namespace CompileHelpers

        namespace DrawExecutionHelpers {
        bool CreateUploadResource(const void* data, size_t dataSize, ComPtr<ID3D12Resource>& outResource) {
            outResource.Reset();
            if (!s_Device || !data || dataSize == 0) {
                return false;
            }

            const UINT64 alignedSize = (static_cast<UINT64>(dataSize) + 255ull) & ~255ull;
            D3D12_HEAP_PROPERTIES heapProps = {};
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
            heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            heapProps.CreationNodeMask = 1;
            heapProps.VisibleNodeMask = 1;

            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Alignment = 0;
            desc.Width = alignedSize;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = D3D12_RESOURCE_FLAG_NONE;

            if (FAILED(s_Device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&outResource))) || !outResource) {
                return false;
            }

            void* mapped = nullptr;
            if (FAILED(outResource->Map(0, nullptr, &mapped)) || !mapped) {
                outResource.Reset();
                return false;
            }
            std::memcpy(mapped, data, dataSize);
            outResource->Unmap(0, nullptr);
            return true;
        }

        void DrainDeferredReleases() {
            if (s_DeferredReleases.empty()) {
                return;
            }
            if (!DX12Init::g_fence) {
                s_DeferredReleases.clear();
                return;
            }

            const UINT64 completed = DX12Init::g_fence->GetCompletedValue();
            auto it = s_DeferredReleases.begin();
            while (it != s_DeferredReleases.end()) {
                if (completed >= it->FenceValue) {
                    it = s_DeferredReleases.erase(it);
                } else {
                    ++it;
                }
            }
        }

        float Clamp01(float value) {
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

        float QualityModeScale(GlowQualityMode mode) {
            switch (mode) {
            case GlowQualityMode::Performance: return 0.84f;
            case GlowQualityMode::Balanced: return 1.0f;
            case GlowQualityMode::Ultra: return 1.14f;
            }
            return 1.0f;
        }

        float GlowModeScale(GlowMode mode) {
            switch (mode) {
            case GlowMode::GaussianBloom: return 1.0f;
            case GlowMode::NeonTube: return 0.9f;
            case GlowMode::AmbientSoft: return 1.30f;
            }
            return 1.0f;
        }

        bool RenderSingleDraw(
            QueuedDraw& draw,
            ID3D12GraphicsCommandList* cmd,
            D3D12_CPU_DESCRIPTOR_HANDLE rtv,
            const ImVec2& displaySize,
            const ImVec4* clipRect) {
            if (!cmd || !s_RootSignature) {
                return false;
            }

            auto programIt = s_Programs.find(draw.ProgramHandle);
            if (programIt == s_Programs.end() || !programIt->second.Pso) {
                return false;
            }

            const float safeWidth = (std::max)(1.0f, displaySize.x);
            const float safeHeight = (std::max)(1.0f, displaySize.y);

            D3D12_VIEWPORT viewport = {};
            viewport.TopLeftX = 0.0f;
            viewport.TopLeftY = 0.0f;
            viewport.Width = safeWidth;
            viewport.Height = safeHeight;
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;

            D3D12_RECT scissor = {};
            if (clipRect) {
                float clipMinX = clipRect->x;
                float clipMinY = clipRect->y;
                float clipMaxX = clipRect->z;
                float clipMaxY = clipRect->w;

                if (!std::isfinite(clipMinX) || !std::isfinite(clipMinY) || !std::isfinite(clipMaxX) || !std::isfinite(clipMaxY)) {
                    return false;
                }

                if (clipMinX < 0.0f) clipMinX = 0.0f;
                if (clipMinY < 0.0f) clipMinY = 0.0f;
                if (clipMaxX > safeWidth) clipMaxX = safeWidth;
                if (clipMaxY > safeHeight) clipMaxY = safeHeight;

                if (clipMaxX <= clipMinX || clipMaxY <= clipMinY) {
                    return true;
                }

                scissor.left = static_cast<LONG>(std::floor(clipMinX));
                scissor.top = static_cast<LONG>(std::floor(clipMinY));
                scissor.right = static_cast<LONG>(std::ceil(clipMaxX));
                scissor.bottom = static_cast<LONG>(std::ceil(clipMaxY));
            } else {
                scissor.left = 0;
                scissor.top = 0;
                scissor.right = static_cast<LONG>(safeWidth);
                scissor.bottom = static_cast<LONG>(safeHeight);
            }

            draw.Globals.Params2[0] = safeWidth;
            draw.Globals.Params2[1] = safeHeight;

            ComPtr<ID3D12Resource> globalsBuffer;
            if (!CreateUploadResource(&draw.Globals, sizeof(draw.Globals), globalsBuffer) || !globalsBuffer) {
                return false;
            }

            static const std::array<uint8_t, 256> kZeroBytes = {};
            ComPtr<ID3D12Resource> userBuffer;
            if (!draw.UserBytes.empty()) {
                if (!CreateUploadResource(draw.UserBytes.data(), draw.UserBytes.size(), userBuffer) || !userBuffer) {
                    return false;
                }
            } else {
                if (!CreateUploadResource(kZeroBytes.data(), kZeroBytes.size(), userBuffer) || !userBuffer) {
                    return false;
                }
            }

            cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            cmd->RSSetViewports(1, &viewport);
            cmd->RSSetScissorRects(1, &scissor);
            cmd->SetGraphicsRootSignature(s_RootSignature.Get());
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->SetPipelineState(programIt->second.Pso.Get());
            cmd->SetGraphicsRootConstantBufferView(0, globalsBuffer->GetGPUVirtualAddress());
            cmd->SetGraphicsRootConstantBufferView(1, userBuffer->GetGPUVirtualAddress());
            cmd->DrawInstanced(6, 1, 0, 0);

            const UINT64 releaseFence = DX12Init::g_fenceLastSignaledValue + 1;
            s_DeferredReleases.push_back({ releaseFence, globalsBuffer });
            s_DeferredReleases.push_back({ releaseFence, userBuffer });
            return true;
        }

        void ShaderImDrawCallback(const ImDrawList*, const ImDrawCmd* cmd) {
            if (!cmd || !cmd->UserCallbackData) {
                return;
            }
            if (!s_ImGuiPass.Active || !s_ImGuiPass.CommandList || !s_RootSignature) {
                ++s_ImGuiPass.CallbackRuntimeFailureCount;
                return;
            }

            auto* draw = static_cast<QueuedDraw*>(cmd->UserCallbackData);
            if (!draw) {
                ++s_ImGuiPass.CallbackRuntimeFailureCount;
                return;
            }

            ImVec4 clipRect = cmd->ClipRect;
            const ImDrawData* drawData = ImGui::GetDrawData();
            if (drawData) {
                const ImVec2 clipOff = drawData->DisplayPos;
                const ImVec2 clipScale = drawData->FramebufferScale;
                clipRect.x = (clipRect.x - clipOff.x) * clipScale.x;
                clipRect.y = (clipRect.y - clipOff.y) * clipScale.y;
                clipRect.z = (clipRect.z - clipOff.x) * clipScale.x;
                clipRect.w = (clipRect.w - clipOff.y) * clipScale.y;
            }
            if (!RenderSingleDraw(*draw, s_ImGuiPass.CommandList, s_ImGuiPass.RTV, s_ImGuiPass.DisplaySize, &clipRect)) {
                ++s_ImGuiPass.CallbackRuntimeFailureCount;
            }
        }
        } // namespace DrawExecutionHelpers

        bool ShouldCompileNow(const ShaderComponent& shader) {
            if (!shader.Enabled) {
                return false;
            }
            if (shader.CompileRequested) {
                return true;
            }
            if (shader.CompilePolicy == ShaderCompilePolicy::StartupPrecompile) {
                return shader.Dirty || !shader.IsCompiled;
            }
            return false;
        }

        void UpdateAutoReloadState(ShaderComponent& shader) {
            if (!shader.AutoReloadFileChanges) {
                return;
            }

            auto checkFile = [&shader](const ShaderSourceSpec& source) {
                std::string resolvedPath;
                std::string resolveError;
                if (!SourceResolver::ResolvePath(source, resolvedPath, resolveError)) {
                    return;
                }
                if (resolvedPath.empty()) {
                    return;
                }

                std::error_code ec;
                const std::filesystem::path shaderPath(resolvedPath);
                if (!std::filesystem::exists(shaderPath, ec) || ec) {
                    return;
                }
                const auto writeTime = std::filesystem::last_write_time(shaderPath, ec);
                if (ec) {
                    return;
                }

                auto it = s_FileWriteTimes.find(resolvedPath);
                if (it == s_FileWriteTimes.end()) {
                    s_FileWriteTimes[resolvedPath] = writeTime;
                    return;
                }
                if (it->second != writeTime) {
                    it->second = writeTime;
                    shader.Dirty = true;
                    if (shader.CompilePolicy != ShaderCompilePolicy::ManualApply) {
                        shader.CompileRequested = true;
                    }
                }
            };

            checkFile(shader.PixelSource);
            if (shader.StageMode == ShaderStageMode::VertexAndPixel) {
                checkFile(shader.VertexSource);
            }
            ShaderSourceSpec sharedPreludeSource;
            sharedPreludeSource.Mode = ShaderSourceMode::EmbeddedCpp;
            sharedPreludeSource.KeyOrPathOrInline = "shader.shared";
            checkFile(sharedPreludeSource);
        }

        bool ValidateSourceSpec(ShaderSourceSpec& source, bool isVertex, bool forGlow, bool forShadow) {
            if (source.EntryPoint.empty()) {
                source.EntryPoint = "main";
            }
            if (source.TargetProfile.empty()) {
                source.TargetProfile = isVertex ? "vs_5_0" : "ps_5_0";
            }

            if (!source.KeyOrPathOrInline.empty()) {
                return true;
            }

            if (source.Mode == ShaderSourceMode::File) {
                if (isVertex) {
                    source.KeyOrPathOrInline = kDefaultVertexPath;
                } else if (forGlow) {
                    source.KeyOrPathOrInline = kDefaultGlowPath;
                } else if (forShadow) {
                    source.KeyOrPathOrInline = kDefaultShadowPath;
                } else {
                    source.KeyOrPathOrInline = kDefaultGlowPath;
                }
                return true;
            }

            if (source.Mode == ShaderSourceMode::EmbeddedCpp || source.Mode == ShaderSourceMode::EmbeddedRust) {
                if (isVertex) {
                    source.KeyOrPathOrInline = "shader.default.vs";
                } else if (forGlow) {
                    source.KeyOrPathOrInline = "glow.default";
                } else if (forShadow) {
                    source.KeyOrPathOrInline = "shadow.default";
                } else {
                    source.KeyOrPathOrInline = "glow.default";
                }
            }

            return true;
        }

        bool PrepareShaderSources(
            ShaderSourceSpec vsSource,
            ShaderSourceSpec psSource,
            const std::vector<ShaderParameter>& params,
            ShaderStageMode stageMode,
            bool forGlow,
            bool forShadow,
            std::string& outVsText,
            std::string& outPsText,
            std::string& outKey,
            std::string& outError) {
            outVsText.clear();
            outPsText.clear();
            outKey.clear();
            outError.clear();

            ValidateSourceSpec(vsSource, true, forGlow, forShadow);
            ValidateSourceSpec(psSource, false, forGlow, forShadow);

            std::string resolveError;
            if (!SourceResolver::ResolveText(vsSource, outVsText, resolveError) || outVsText.empty()) {
                outError = "Vertex source resolution failed (" + ToString(vsSource.Mode) + "): " + resolveError;
                return false;
            }
            if (!SourceResolver::ResolveText(psSource, outPsText, resolveError) || outPsText.empty()) {
                outError = "Pixel source resolution failed (" + ToString(psSource.Mode) + "): " + resolveError;
                return false;
            }
            std::string sharedPrelude;
            if (!SourceResolver::ResolveSharedPrelude(sharedPrelude, resolveError) || sharedPrelude.empty()) {
                outError = "Shared shader prelude resolution failed: " + resolveError;
                return false;
            }

            ShaderParamBuilder builder;
            builder.Set(params);
            const std::string userDecl = builder.BuildCBufferHlsl("UIUserParams");

            if (outVsText.find("UI_SHADER_SHARED_TYPES") == std::string::npos) {
                outVsText = sharedPrelude + "\n" + userDecl + "\n" + outVsText;
            } else if (outVsText.find("cbuffer UIUserParams") == std::string::npos) {
                outVsText = userDecl + "\n" + outVsText;
            }
            if (outPsText.find("UI_SHADER_SHARED_TYPES") == std::string::npos) {
                outPsText = sharedPrelude + "\n" + userDecl + "\n" + outPsText;
            } else if (outPsText.find("cbuffer UIUserParams") == std::string::npos) {
                outPsText = userDecl + "\n" + outPsText;
            }

            const std::string userParamSig = BuildUserParamSignature(params);
            const BlendMode blend = forGlow ? BlendMode::Additive : BlendMode::Alpha;
            outKey = BuildShaderCacheKey(
                blend,
                stageMode,
                vsSource,
                psSource,
                userParamSig,
                forGlow,
                forShadow,
                outVsText,
                outPsText);
            return true;
        }

        bool AcquireProgram(
            ShaderSourceSpec vsSource,
            ShaderSourceSpec psSource,
            const std::vector<ShaderParameter>& params,
            ShaderStageMode stageMode,
            ShaderBackendMode backend,
            bool forGlow,
            bool forShadow,
            uint64_t& outHandle,
            ShaderBackendMode& outBackendUsed,
            std::string& outError) {
            outHandle = 0;
            outError.clear();

            if (!s_Device) {
                outError = "ShaderSystem not initialized.";
                return false;
            }

            std::string rootError;
            if (!CompileHelpers::EnsureRootSignature(rootError)) {
                outError = rootError;
                return false;
            }

            std::string vsText;
            std::string psText;
            std::string cacheKey;
            if (!PrepareShaderSources(vsSource, psSource, params, stageMode, forGlow, forShadow, vsText, psText, cacheKey, outError)) {
                return false;
            }

            const auto existing = s_ProgramByKey.find(cacheKey);
            if (existing != s_ProgramByKey.end()) {
                const auto foundProgram = s_Programs.find(existing->second);
                if (foundProgram != s_Programs.end() && foundProgram->second.Pso) {
                    outHandle = foundProgram->second.Handle;
                    outBackendUsed = foundProgram->second.BackendUsed;
                    return true;
                }
            }

            std::vector<uint8_t> vsBytecode;
            std::vector<uint8_t> psBytecode;
            ShaderBackendMode backendUsed = ShaderBackendMode::D3DCompileOnly;
            ShaderBackendMode backendUsedPs = ShaderBackendMode::D3DCompileOnly;

            if (!CompileHelpers::CompileShaderBytecode(vsText, vsSource.EntryPoint, vsSource.TargetProfile, backend, vsBytecode, backendUsed, outError)) {
                outError = "Vertex compile failed: " + outError;
                return false;
            }
            if (!CompileHelpers::CompileShaderBytecode(psText, psSource.EntryPoint, psSource.TargetProfile, backend, psBytecode, backendUsedPs, outError)) {
                outError = "Pixel compile failed: " + outError;
                return false;
            }

            const BlendMode blend = forGlow ? BlendMode::Additive : BlendMode::Alpha;
            ComPtr<ID3D12PipelineState> pso;
            if (!CompileHelpers::CreatePipelineState(vsBytecode, psBytecode, blend, pso, outError)) {
                return false;
            }

            CompiledProgram program;
            program.Handle = s_NextProgramHandle++;
            program.BackendUsed = backendUsed;
            program.Blend = blend;
            program.CacheKey = cacheKey;
            program.Pso = pso;
            program.LastError.clear();

            s_ProgramByKey[cacheKey] = program.Handle;
            s_Programs[program.Handle] = std::move(program);

            outHandle = s_ProgramByKey[cacheKey];
            outBackendUsed = backendUsed;
            return true;
        }

        ShaderSourceSpec BuildDefaultVertexSource() {
            ShaderSourceSpec source;
            source.Mode = ShaderSourceMode::File;
            source.KeyOrPathOrInline = kDefaultVertexPath;
            source.EntryPoint = "main";
            source.TargetProfile = "vs_5_0";
            return source;
        }

        ShaderSourceSpec BuildDefaultPixelSource(bool forGlow, bool forShadow, const std::string& keyOverride) {
            (void)forShadow;
            ShaderSourceSpec source;
            source.Mode = ShaderSourceMode::File;
            source.KeyOrPathOrInline = forGlow ? kDefaultGlowPath : kDefaultShadowPath;
            if (!keyOverride.empty()) {
                if (keyOverride == "glow.default") {
                    source.Mode = ShaderSourceMode::File;
                    source.KeyOrPathOrInline = kDefaultGlowPath;
                } else if (keyOverride == "shadow.default") {
                    source.Mode = ShaderSourceMode::File;
                    source.KeyOrPathOrInline = kDefaultShadowPath;
                } else if (LooksLikeFilePath(keyOverride)) {
                    source.Mode = ShaderSourceMode::File;
                    source.KeyOrPathOrInline = keyOverride;
                } else {
                    source.Mode = ShaderSourceMode::EmbeddedCpp;
                    source.KeyOrPathOrInline = keyOverride;
                }
            }
            source.EntryPoint = "main";
            source.TargetProfile = "ps_5_0";
            return source;
        }
        bool BuildQueueEntry(
            entt::registry& registry,
            entt::entity entity,
            const ImVec2& pMin,
            const ImVec2& pMax,
            int zOrder,
            float alpha,
            bool forGlow,
            bool forShadow,
            QueuedDraw& outEntry,
            std::string& outError) {
            outError.clear();

            if (!registry.valid(entity)) {
                outError = "Invalid entity.";
                return false;
            }

            if (!forGlow && !forShadow) {
                outError = "QueueEntity requires glow or shadow flag.";
                return false;
            }

            ShaderComponent* shader = registry.try_get<ShaderComponent>(entity);
            GlowComponent* glow = registry.try_get<GlowComponent>(entity);
            ShadowComponent* shadow = registry.try_get<ShadowComponent>(entity);

            if (forGlow) {
                if (!glow || !glow->Enabled || glow->RenderMode != GlowRenderMode::Shader) {
                    outError = "Glow shader mode disabled.";
                    return false;
                }
            }
            if (forShadow) {
                if (!shadow || !shadow->Enabled || shadow->RenderMode != ShadowRenderMode::Shader) {
                    outError = "Shadow shader mode disabled.";
                    return false;
                }
            }

            ShaderBackendMode backend = ShaderBackendMode::AutoPreferDXC;
            ShaderCompilePolicy policy = ShaderCompilePolicy::OnDemandCache;
            ShaderSourceSpec vertexSource = BuildDefaultVertexSource();
            ShaderSourceSpec pixelSource = BuildDefaultPixelSource(
                forGlow,
                forShadow,
                forGlow && glow ? glow->ShaderKey : (forShadow && shadow ? shadow->ShaderKey : std::string{}));
            std::vector<ShaderParameter> params;
            ShaderStageMode stageMode = ShaderStageMode::PixelOnly;

            if (shader && shader->Enabled) {
                backend = shader->Backend;
                policy = shader->CompilePolicy;
                stageMode = shader->StageMode;
                params = shader->Parameters;

                if (shader->PixelSource.Mode != ShaderSourceMode::Inline && shader->PixelSource.KeyOrPathOrInline.empty()) {
                    shader->PixelSource = pixelSource;
                }
                if (shader->VertexSource.Mode != ShaderSourceMode::Inline && shader->VertexSource.KeyOrPathOrInline.empty()) {
                    shader->VertexSource = BuildDefaultVertexSource();
                }

                pixelSource = shader->PixelSource;
                vertexSource = (stageMode == ShaderStageMode::VertexAndPixel) ? shader->VertexSource : BuildDefaultVertexSource();
            }

            if (policy == ShaderCompilePolicy::ManualApply) {
                if (!shader || !shader->CompileRequested) {
                    if (shader) {
                        MarkShaderFailure(shader, "ManualApply selected: press Apply/Recompile.");
                    }
                    outError = "ManualApply selected: compile not requested.";
                    return false;
                }
            }

            uint64_t programHandle = 0;
            ShaderBackendMode backendUsed = ShaderBackendMode::D3DCompileOnly;
            if (!AcquireProgram(vertexSource, pixelSource, params, stageMode, backend, forGlow, forShadow, programHandle, backendUsed, outError)) {
                if (shader) {
                    MarkShaderFailure(shader, outError);
                }
                return false;
            }

            if (shader) {
                shader->IsCompiled = true;
                shader->Dirty = false;
                shader->CompileRequested = false;
                shader->LastError.clear();
                shader->ProgramHandle = programHandle;
            }

            outEntry = {};
            outEntry.Entity = entity;
            outEntry.ZOrder = zOrder;
            outEntry.EntityId = static_cast<uint64_t>(entt::to_integral(entity));
            outEntry.ProgramHandle = programHandle;

            ShaderGlobals globals{};
            globals.RectMinMax[0] = pMin.x;
            globals.RectMinMax[1] = pMin.y;
            globals.RectMinMax[2] = pMax.x;
            globals.RectMinMax[3] = pMax.y;
            globals.DrawMinMax[0] = pMin.x;
            globals.DrawMinMax[1] = pMin.y;
            globals.DrawMinMax[2] = pMax.x;
            globals.DrawMinMax[3] = pMax.y;

            if (forGlow && glow) {
                globals.Color[0] = static_cast<float>((glow->Color >> 0) & 0xFF) / 255.0f;
                globals.Color[1] = static_cast<float>((glow->Color >> 8) & 0xFF) / 255.0f;
                globals.Color[2] = static_cast<float>((glow->Color >> 16) & 0xFF) / 255.0f;
                globals.Color[3] = static_cast<float>((glow->Color >> 24) & 0xFF) / 255.0f;

                globals.Params0[0] = (std::max)(0.0f, glow->Radius * glow->RadiusScale);
                globals.Params0[1] = (std::max)(0.0f, glow->Intensity);
                globals.Params0[2] = (std::max)(0.2f, glow->Falloff);
                globals.Params0[3] = (std::max)(0.0f, glow->CoreStrength);

                globals.Params1[0] = static_cast<float>(static_cast<int>(glow->Mode));
                globals.Params1[1] = glow->OuterOnly ? 1.0f : 0.0f;
                globals.Params1[2] = glow->InnerGlow ? 1.0f : 0.0f;
                globals.Params1[3] = static_cast<float>(static_cast<int>(glow->QualityMode));

                const float glowMargin =
                    (std::max)(
                        0.0f,
                        glow->Radius * glow->RadiusScale * QualityModeScale(glow->QualityMode) * GlowModeScale(glow->Mode) + 4.0f);
                globals.DrawMinMax[0] -= glowMargin;
                globals.DrawMinMax[1] -= glowMargin;
                globals.DrawMinMax[2] += glowMargin;
                globals.DrawMinMax[3] += glowMargin;
            } else if (forShadow && shadow) {
                globals.Color[0] = static_cast<float>((shadow->Color >> 0) & 0xFF) / 255.0f;
                globals.Color[1] = static_cast<float>((shadow->Color >> 8) & 0xFF) / 255.0f;
                globals.Color[2] = static_cast<float>((shadow->Color >> 16) & 0xFF) / 255.0f;
                globals.Color[3] = static_cast<float>((shadow->Color >> 24) & 0xFF) / 255.0f;

                globals.Params0[0] = 0.0f;
                globals.Params0[1] = 1.0f;
                globals.Params0[2] = 1.0f;
                globals.Params0[3] = 0.0f;

                globals.Params1[0] = 0.0f;
                globals.Params1[1] = shadow->Inset ? 1.0f : 0.0f;
                globals.Params1[2] = 0.0f;
                globals.Params1[3] = 0.0f;

                globals.Params3[0] = shadow->Offset.x;
                globals.Params3[1] = shadow->Offset.y;
                globals.Params3[2] = (std::max)(0.5f, shadow->BlurRadius);
                globals.Params3[3] = shadow->Spread;

                const float shadowMarginX =
                    std::fabs(shadow->Offset.x) + (std::max)(0.5f, shadow->BlurRadius) * 2.0f + std::fabs(shadow->Spread) + 4.0f;
                const float shadowMarginY =
                    std::fabs(shadow->Offset.y) + (std::max)(0.5f, shadow->BlurRadius) * 2.0f + std::fabs(shadow->Spread) + 4.0f;
                globals.DrawMinMax[0] -= shadowMarginX;
                globals.DrawMinMax[1] -= shadowMarginY;
                globals.DrawMinMax[2] += shadowMarginX;
                globals.DrawMinMax[3] += shadowMarginY;
            }

            globals.Params2[0] = 0.0f;
            globals.Params2[1] = 0.0f;
            globals.Params2[2] = static_cast<float>(ImGui::GetTime());
            globals.Params2[3] = Clamp01(alpha);

            outEntry.Globals = globals;

            ShaderParamBuilder builder;
            builder.Set(params);
            builder.BuildPackedCBuffer(outEntry.UserBytes);
            return true;
        }
    } // namespace

    void Initialize(ID3D12Device* device) {
        s_Device = device;
        s_RootSignature.Reset();
        s_SourceAliases.clear();
        SourceAliasRegistry::SeedDefaults();
        s_FileWriteTimes.clear();
        s_ProgramByKey.clear();
        s_Programs.clear();
        s_QueuedDraws.clear();
        s_ImGuiCallbackDraws.clear();
        s_ImGuiPass = {};
        s_LastImGuiCallbackQueuedCount = 0;
        s_LastImGuiCallbackRuntimeFailureCount = 0;
        s_DeferredReleases.clear();
        s_NextProgramHandle = 1;
    }

    void Shutdown() {
        s_QueuedDraws.clear();
        s_ImGuiCallbackDraws.clear();
        s_ImGuiPass = {};
        s_LastImGuiCallbackQueuedCount = 0;
        s_LastImGuiCallbackRuntimeFailureCount = 0;
        s_DeferredReleases.clear();
        s_Programs.clear();
        s_ProgramByKey.clear();
        s_SourceAliases.clear();
        s_FileWriteTimes.clear();
        s_RootSignature.Reset();
        s_Device = nullptr;
    }

    void RegisterSourceAlias(const std::string& key, const std::string& path) {
        if (key.empty()) {
            return;
        }
        s_SourceAliases[key] = path;
    }

    void UnregisterSourceAlias(const std::string& key) {
        if (key.empty()) {
            return;
        }
        s_SourceAliases.erase(key);
    }

    void ResetDefaultSourceAliases() {
        s_SourceAliases.clear();
        SourceAliasRegistry::SeedDefaults();
    }

    void BeginImGuiPass(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE rtv, const ImVec2& displaySize) {
        s_ImGuiPass.CommandList = cmd;
        s_ImGuiPass.RTV = rtv;
        s_ImGuiPass.DisplaySize = ImVec2((std::max)(1.0f, displaySize.x), (std::max)(1.0f, displaySize.y));
        s_ImGuiPass.Active = (cmd != nullptr);
        s_ImGuiPass.CallbackQueuedCount = 0;
        s_ImGuiPass.CallbackRuntimeFailureCount = 0;
        s_ImGuiCallbackDraws.clear();
        DrawExecutionHelpers::DrainDeferredReleases();
    }

    void EndImGuiPass() {
        s_LastImGuiCallbackQueuedCount = s_ImGuiPass.CallbackQueuedCount;
        s_LastImGuiCallbackRuntimeFailureCount = s_ImGuiPass.CallbackRuntimeFailureCount;
        s_ImGuiPass.Active = false;
        s_ImGuiPass.CommandList = nullptr;
        s_ImGuiCallbackDraws.clear();
    }

    bool TryQueueEntity(
        entt::registry& registry,
        entt::entity entity,
        const ImVec2& pMin,
        const ImVec2& pMax,
        int zOrder,
        float alpha,
        bool forGlow,
        bool forShadow) {
        QueuedDraw entry;
        std::string error;
        if (!BuildQueueEntry(registry, entity, pMin, pMax, zOrder, alpha, forGlow, forShadow, entry, error)) {
            return false;
        }
        if (s_QueuedDraws.size() >= kMaxQueuedDrawsPerFrame) {
            return false;
        }

        s_QueuedDraws.push_back(std::move(entry));
        return true;
    }

    bool TryQueueEntityImGui(
        entt::registry& registry,
        entt::entity entity,
        ImDrawList* drawList,
        const ImVec2& objectMin,
        const ImVec2& objectMax,
        int zOrder,
        float alpha,
        bool forGlow,
        bool forShadow) {
        if (!drawList || !s_ImGuiPass.Active || !s_ImGuiPass.CommandList) {
            return false;
        }

        QueuedDraw entry;
        std::string error;
        if (!BuildQueueEntry(registry, entity, objectMin, objectMax, zOrder, alpha, forGlow, forShadow, entry, error)) {
            return false;
        }

        if (s_ImGuiCallbackDraws.size() >= kMaxQueuedDrawsPerFrame) {
            return false;
        }

        auto storedDraw = std::make_unique<QueuedDraw>(std::move(entry));
        QueuedDraw* callbackData = storedDraw.get();
        s_ImGuiCallbackDraws.push_back(std::move(storedDraw));
        drawList->AddCallback(DrawExecutionHelpers::ShaderImDrawCallback, callbackData);
        drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        ++s_ImGuiPass.CallbackQueuedCount;
        return true;
    }

    void QueueEntity(
        entt::registry& registry,
        entt::entity entity,
        const ImVec2& pMin,
        const ImVec2& pMax,
        int zOrder,
        float alpha,
        bool forGlow,
        bool forShadow) {
        (void)TryQueueEntity(registry, entity, pMin, pMax, zOrder, alpha, forGlow, forShadow);
    }

    void UpdateCompile(entt::registry& registry) {
        auto view = registry.view<ShaderComponent>();
        for (auto entity : view) {
            auto& shader = view.get<ShaderComponent>(entity);
            UpdateAutoReloadState(shader);
            if (!ShouldCompileNow(shader)) {
                continue;
            }

            const bool hasGlow = registry.any_of<GlowComponent>(entity) &&
                registry.get<GlowComponent>(entity).RenderMode == GlowRenderMode::Shader;
            const bool hasShadow = registry.any_of<ShadowComponent>(entity) &&
                registry.get<ShadowComponent>(entity).RenderMode == ShadowRenderMode::Shader;

            const bool forGlow = hasGlow;
            const bool forShadow = !hasGlow && hasShadow;

            ShaderSourceSpec vertexSource = shader.StageMode == ShaderStageMode::VertexAndPixel
                ? shader.VertexSource
                : BuildDefaultVertexSource();

            ShaderSourceSpec pixelSource = shader.PixelSource;
            if (pixelSource.KeyOrPathOrInline.empty()) {
                if (forGlow && registry.any_of<GlowComponent>(entity)) {
                    pixelSource = BuildDefaultPixelSource(true, false, registry.get<GlowComponent>(entity).ShaderKey);
                } else if (forShadow && registry.any_of<ShadowComponent>(entity)) {
                    pixelSource = BuildDefaultPixelSource(false, true, registry.get<ShadowComponent>(entity).ShaderKey);
                } else {
                    pixelSource = BuildDefaultPixelSource(true, false, std::string{});
                }
            }

            uint64_t handle = 0;
            ShaderBackendMode backendUsed = ShaderBackendMode::D3DCompileOnly;
            std::string error;
            if (!AcquireProgram(
                vertexSource,
                pixelSource,
                shader.Parameters,
                shader.StageMode,
                shader.Backend,
                forGlow,
                forShadow,
                handle,
                backendUsed,
                error)) {
                MarkShaderFailure(&shader, error);
                shader.CompileRequested = false;
                continue;
            }

            shader.IsCompiled = true;
            shader.Dirty = false;
            shader.CompileRequested = false;
            shader.LastError.clear();
            shader.ProgramHandle = handle;
        }
    }

    void RenderQueued(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE rtv, const ImVec2& displaySize) {
        if (!cmd || s_QueuedDraws.empty() || !s_RootSignature) {
            return;
        }

        DrawExecutionHelpers::DrainDeferredReleases();

        std::sort(s_QueuedDraws.begin(), s_QueuedDraws.end(), [](const QueuedDraw& lhs, const QueuedDraw& rhs) {
            if (lhs.ZOrder != rhs.ZOrder) {
                return lhs.ZOrder < rhs.ZOrder;
            }
            return lhs.EntityId < rhs.EntityId;
        });

        for (auto& draw : s_QueuedDraws) {
            (void)DrawExecutionHelpers::RenderSingleDraw(draw, cmd, rtv, displaySize, nullptr);
        }

        s_QueuedDraws.clear();
    }

    void ClearQueue() {
        s_QueuedDraws.clear();
        s_ImGuiCallbackDraws.clear();
    }

    size_t QueryQueuedCount() {
        return s_QueuedDraws.size();
    }

    size_t QueryImGuiCallbackQueuedCount() {
        return s_LastImGuiCallbackQueuedCount;
    }

    size_t QueryImGuiCallbackRuntimeFailureCount() {
        return s_LastImGuiCallbackRuntimeFailureCount;
    }
} // namespace RenderUtils::ShaderSystem
