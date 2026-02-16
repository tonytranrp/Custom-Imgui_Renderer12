#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

#include "imgui.h"

namespace RenderUtils {
    enum class ShaderSourceMode {
        EmbeddedCpp,
        EmbeddedRust,
        File,
        Inline
    };

    enum class ShaderBackendMode {
        AutoPreferDXC,
        D3DCompileOnly,
        DXCOnly
    };

    enum class ShaderCompilePolicy {
        OnDemandCache,
        StartupPrecompile,
        ManualApply
    };

    enum class ShaderStageMode {
        PixelOnly,
        VertexAndPixel
    };

    enum class ShaderParamType {
        Float,
        Int,
        Vec2,
        Vec3,
        Vec4,
        Color,
        Bool
    };

    struct ShaderParameter {
        std::string Name;
        ShaderParamType Type = ShaderParamType::Float;
        std::variant<float, int, ImVec2, ImVec4, ImU32, bool> Value = 0.0f;
        bool ExposedInInspector = true;
    };

    struct ShaderSourceSpec {
        ShaderSourceMode Mode = ShaderSourceMode::File;
        std::string KeyOrPathOrInline;
        std::string EntryPoint = "main";
        std::string TargetProfile = "ps_5_0";

        ShaderSourceSpec() = default;

        ShaderSourceSpec& SetMode(ShaderSourceMode mode) {
            Mode = mode;
            return *this;
        }

        ShaderSourceSpec& SetSource(const std::string& value) {
            KeyOrPathOrInline = value;
            return *this;
        }

        ShaderSourceSpec& SetEntry(const std::string& entry) {
            EntryPoint = entry;
            return *this;
        }

        ShaderSourceSpec& SetTarget(const std::string& target) {
            TargetProfile = target;
            return *this;
        }
    };

    class ShaderParamBuilder {
    public:
        ShaderParamBuilder& Float(const std::string& name, float value) {
            Add(name, ShaderParamType::Float, value);
            return *this;
        }

        ShaderParamBuilder& Int(const std::string& name, int value) {
            Add(name, ShaderParamType::Int, value);
            return *this;
        }

        ShaderParamBuilder& Vec2(const std::string& name, const ImVec2& value) {
            Add(name, ShaderParamType::Vec2, value);
            return *this;
        }

        ShaderParamBuilder& Vec3(const std::string& name, const ImVec4& value) {
            Add(name, ShaderParamType::Vec3, value);
            return *this;
        }

        ShaderParamBuilder& Vec4(const std::string& name, const ImVec4& value) {
            Add(name, ShaderParamType::Vec4, value);
            return *this;
        }

        ShaderParamBuilder& Color(const std::string& name, ImU32 value) {
            Add(name, ShaderParamType::Color, value);
            return *this;
        }

        ShaderParamBuilder& Bool(const std::string& name, bool value) {
            Add(name, ShaderParamType::Bool, value);
            return *this;
        }

        ShaderParamBuilder& Clear() {
            m_Params.clear();
            return *this;
        }

        ShaderParamBuilder& Set(const std::vector<ShaderParameter>& params) {
            m_Params = params;
            return *this;
        }

        const std::vector<ShaderParameter>& Parameters() const {
            return m_Params;
        }

        std::string BuildCBufferHlsl(const std::string& blockName = "UIUserParams") const {
            std::string hlsl;
            hlsl += "cbuffer ";
            hlsl += blockName;
            hlsl += " : register(b1)\n{\n";
            for (const auto& param : m_Params) {
                const std::string safeName = SanitizeName(param.Name);
                switch (param.Type) {
                case ShaderParamType::Float:
                    hlsl += "    float4 ";
                    hlsl += safeName;
                    hlsl += "; // float in .x\n";
                    break;
                case ShaderParamType::Int:
                    hlsl += "    int4 ";
                    hlsl += safeName;
                    hlsl += "; // int in .x\n";
                    break;
                case ShaderParamType::Vec2:
                    hlsl += "    float4 ";
                    hlsl += safeName;
                    hlsl += "; // vec2 in .xy\n";
                    break;
                case ShaderParamType::Vec3:
                    hlsl += "    float4 ";
                    hlsl += safeName;
                    hlsl += "; // vec3 in .xyz\n";
                    break;
                case ShaderParamType::Vec4:
                    hlsl += "    float4 ";
                    hlsl += safeName;
                    hlsl += ";\n";
                    break;
                case ShaderParamType::Color:
                    hlsl += "    float4 ";
                    hlsl += safeName;
                    hlsl += "; // rgba 0..1\n";
                    break;
                case ShaderParamType::Bool:
                    hlsl += "    int4 ";
                    hlsl += safeName;
                    hlsl += "; // bool in .x\n";
                    break;
                }
            }
            hlsl += "};\n";
            return hlsl;
        }

        void BuildPackedCBuffer(std::vector<uint8_t>& outBytes) const {
            outBytes.clear();
            outBytes.reserve(m_Params.size() * 16u);
            for (const auto& param : m_Params) {
                switch (param.Type) {
                case ShaderParamType::Float: {
                    const float v = std::holds_alternative<float>(param.Value) ? std::get<float>(param.Value) : 0.0f;
                    PushFloat4(outBytes, v, 0.0f, 0.0f, 0.0f);
                    break;
                }
                case ShaderParamType::Int: {
                    const int v = std::holds_alternative<int>(param.Value) ? std::get<int>(param.Value) : 0;
                    PushInt4(outBytes, v, 0, 0, 0);
                    break;
                }
                case ShaderParamType::Vec2: {
                    const ImVec2 v = std::holds_alternative<ImVec2>(param.Value) ? std::get<ImVec2>(param.Value) : ImVec2(0.0f, 0.0f);
                    PushFloat4(outBytes, v.x, v.y, 0.0f, 0.0f);
                    break;
                }
                case ShaderParamType::Vec3: {
                    const ImVec4 v = std::holds_alternative<ImVec4>(param.Value) ? std::get<ImVec4>(param.Value) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
                    PushFloat4(outBytes, v.x, v.y, v.z, 0.0f);
                    break;
                }
                case ShaderParamType::Vec4: {
                    const ImVec4 v = std::holds_alternative<ImVec4>(param.Value) ? std::get<ImVec4>(param.Value) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
                    PushFloat4(outBytes, v.x, v.y, v.z, v.w);
                    break;
                }
                case ShaderParamType::Color: {
                    const ImU32 c = std::holds_alternative<ImU32>(param.Value) ? std::get<ImU32>(param.Value) : IM_COL32(255, 255, 255, 255);
                    const float r = static_cast<float>((c >> 0) & 0xFF) / 255.0f;
                    const float g = static_cast<float>((c >> 8) & 0xFF) / 255.0f;
                    const float b = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
                    const float a = static_cast<float>((c >> 24) & 0xFF) / 255.0f;
                    PushFloat4(outBytes, r, g, b, a);
                    break;
                }
                case ShaderParamType::Bool: {
                    const bool b = std::holds_alternative<bool>(param.Value) ? std::get<bool>(param.Value) : false;
                    PushInt4(outBytes, b ? 1 : 0, 0, 0, 0);
                    break;
                }
                }
            }
        }

    private:
        std::vector<ShaderParameter> m_Params;

        static std::string SanitizeName(const std::string& name) {
            if (name.empty()) {
                return "Param";
            }
            std::string out = name;
            for (size_t i = 0; i < out.size(); ++i) {
                const unsigned char ch = static_cast<unsigned char>(out[i]);
                const bool alphaNum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
                if (!(alphaNum || ch == '_')) {
                    out[i] = '_';
                }
            }
            const unsigned char first = static_cast<unsigned char>(out[0]);
            if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_')) {
                out.insert(out.begin(), '_');
            }
            return out;
        }

        void Add(const std::string& name, ShaderParamType type, const std::variant<float, int, ImVec2, ImVec4, ImU32, bool>& value) {
            const std::string safeName = SanitizeName(name);
            for (auto& param : m_Params) {
                if (param.Name == safeName) {
                    param.Type = type;
                    param.Value = value;
                    return;
                }
            }

            ShaderParameter param;
            param.Name = safeName;
            param.Type = type;
            param.Value = value;
            m_Params.push_back(std::move(param));
        }

        static void PushFloat4(std::vector<uint8_t>& outBytes, float x, float y, float z, float w) {
            const float values[4] = { x, y, z, w };
            const uint8_t* src = reinterpret_cast<const uint8_t*>(values);
            outBytes.insert(outBytes.end(), src, src + sizeof(values));
        }

        static void PushInt4(std::vector<uint8_t>& outBytes, int x, int y, int z, int w) {
            const int values[4] = { x, y, z, w };
            const uint8_t* src = reinterpret_cast<const uint8_t*>(values);
            outBytes.insert(outBytes.end(), src, src + sizeof(values));
        }
    };

    struct ShaderComponent {
        bool Enabled = true;
        ShaderStageMode StageMode = ShaderStageMode::PixelOnly;
        ShaderBackendMode Backend = ShaderBackendMode::AutoPreferDXC;
        ShaderCompilePolicy CompilePolicy = ShaderCompilePolicy::OnDemandCache;
        ShaderSourceSpec VertexSource = ShaderSourceSpec().SetTarget("vs_5_0");
        ShaderSourceSpec PixelSource = ShaderSourceSpec().SetTarget("ps_5_0");
        std::vector<ShaderParameter> Parameters;
        bool Dirty = true;
        bool CompileRequested = false;
        bool AutoReloadFileChanges = true;
        bool IsCompiled = false;
        std::string LastError;
        uint64_t ProgramHandle = 0;

        ShaderComponent& SetEnabled(bool enabled) {
            Enabled = enabled;
            return *this;
        }

        ShaderComponent& SetStageMode(ShaderStageMode mode) {
            StageMode = mode;
            Dirty = true;
            return *this;
        }

        ShaderComponent& SetBackend(ShaderBackendMode mode) {
            Backend = mode;
            Dirty = true;
            return *this;
        }

        ShaderComponent& SetCompilePolicy(ShaderCompilePolicy policy) {
            CompilePolicy = policy;
            return *this;
        }

        ShaderComponent& SetVertexSource(const ShaderSourceSpec& source) {
            VertexSource = source;
            Dirty = true;
            return *this;
        }

        ShaderComponent& SetPixelSource(const ShaderSourceSpec& source) {
            PixelSource = source;
            Dirty = true;
            return *this;
        }

        ShaderComponent& SetParameters(const std::vector<ShaderParameter>& params) {
            Parameters = params;
            Dirty = true;
            return *this;
        }

        ShaderComponent& SetAutoReloadFileChanges(bool enabled) {
            AutoReloadFileChanges = enabled;
            return *this;
        }

        ShaderComponent& SetSourceMode(ShaderSourceMode mode) {
            PixelSource.Mode = mode;
            Dirty = true;
            return *this;
        }

        ShaderComponent& SetPixelSourceValue(const std::string& value) {
            PixelSource.KeyOrPathOrInline = value;
            Dirty = true;
            return *this;
        }

        ShaderComponent& RequestCompile() {
            CompileRequested = true;
            return *this;
        }

        ShaderComponent& SetDirty(bool dirty) {
            Dirty = dirty;
            return *this;
        }
    };
} // namespace RenderUtils
