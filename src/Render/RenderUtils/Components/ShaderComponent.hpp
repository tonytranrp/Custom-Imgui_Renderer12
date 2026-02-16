#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
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

    enum class ShaderAutoUniform {
        None = 0,
        TimeSeconds,
        DeltaSeconds,
        MousePos,
        DisplaySize,
        EntityMin,
        EntityMax,
        EntitySize,
        EntityRect
    };

    enum class ShaderBindingMode {
        Literal = 0,
        BuiltinAutoUniform,
        RegisteredAutoUniform
    };

    using ShaderParamValue = std::variant<float, int, ImVec2, ImVec4, ImU32, bool>;

    struct ShaderParameter {
        std::string Name;
        ShaderParamType Type = ShaderParamType::Float;
        ShaderParamValue Value = 0.0f;
        ShaderBindingMode BindingMode = ShaderBindingMode::Literal;
        std::string UniformKey;
        ShaderAutoUniform AutoUniform = ShaderAutoUniform::None;
        bool ExposedInInspector = true;
    };

    struct ShaderAutoUniformContext {
        float TimeSeconds = 0.0f;
        float DeltaSeconds = 0.0f;
        ImVec2 MousePos = ImVec2(0.0f, 0.0f);
        ImVec2 DisplaySize = ImVec2(1.0f, 1.0f);
        ImVec2 EntityMin = ImVec2(0.0f, 0.0f);
        ImVec2 EntityMax = ImVec2(0.0f, 0.0f);
        ImVec2 EntitySize = ImVec2(0.0f, 0.0f);
        ImVec4 EntityRect = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
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
        using RegisteredUniformResolver = std::function<bool(
            const std::string& uniformKey,
            const ShaderAutoUniformContext& context,
            ShaderParamType expectedType,
            ShaderParamValue& outValue,
            std::string& outError)>;

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

        ShaderParamBuilder& AutoFloat(const std::string& name, ShaderAutoUniform binding, float fallbackValue = 0.0f) {
            AddAuto(name, ShaderParamType::Float, fallbackValue, binding);
            return *this;
        }

        ShaderParamBuilder& AutoVec2(const std::string& name, ShaderAutoUniform binding, const ImVec2& fallbackValue = ImVec2(0.0f, 0.0f)) {
            AddAuto(name, ShaderParamType::Vec2, fallbackValue, binding);
            return *this;
        }

        ShaderParamBuilder& AutoVec4(const std::string& name, ShaderAutoUniform binding, const ImVec4& fallbackValue = ImVec4(0.0f, 0.0f, 0.0f, 0.0f)) {
            AddAuto(name, ShaderParamType::Vec4, fallbackValue, binding);
            return *this;
        }

        ShaderParamBuilder& BindFloat(const std::string& name, const std::string& uniformKey, float fallbackValue = 0.0f) {
            AddBound(name, ShaderParamType::Float, fallbackValue, uniformKey);
            return *this;
        }

        ShaderParamBuilder& BindInt(const std::string& name, const std::string& uniformKey, int fallbackValue = 0) {
            AddBound(name, ShaderParamType::Int, fallbackValue, uniformKey);
            return *this;
        }

        ShaderParamBuilder& BindVec2(const std::string& name, const std::string& uniformKey, const ImVec2& fallbackValue = ImVec2(0.0f, 0.0f)) {
            AddBound(name, ShaderParamType::Vec2, fallbackValue, uniformKey);
            return *this;
        }

        ShaderParamBuilder& BindVec4(const std::string& name, const std::string& uniformKey, const ImVec4& fallbackValue = ImVec4(0.0f, 0.0f, 0.0f, 0.0f)) {
            AddBound(name, ShaderParamType::Vec4, fallbackValue, uniformKey);
            return *this;
        }

        ShaderParamBuilder& BindColor(const std::string& name, const std::string& uniformKey, ImU32 fallbackValue = IM_COL32(255, 255, 255, 255)) {
            AddBound(name, ShaderParamType::Color, fallbackValue, uniformKey);
            return *this;
        }

        ShaderParamBuilder& BindBool(const std::string& name, const std::string& uniformKey, bool fallbackValue = false) {
            AddBound(name, ShaderParamType::Bool, fallbackValue, uniformKey);
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

        void BuildPackedCBuffer(
            std::vector<uint8_t>& outBytes,
            const ShaderAutoUniformContext* autoUniformContext = nullptr,
            const RegisteredUniformResolver* registeredResolver = nullptr,
            std::string* outResolveError = nullptr) const {
            outBytes.clear();
            outBytes.reserve(m_Params.size() * 16u);
            if (outResolveError) {
                outResolveError->clear();
            }
            for (const auto& param : m_Params) {
                const auto resolvedValue = ResolveAutoUniformValue(param, autoUniformContext, registeredResolver, outResolveError);
                switch (param.Type) {
                case ShaderParamType::Float: {
                    const float v = std::holds_alternative<float>(resolvedValue) ? std::get<float>(resolvedValue) : 0.0f;
                    PushFloat4(outBytes, v, 0.0f, 0.0f, 0.0f);
                    break;
                }
                case ShaderParamType::Int: {
                    const int v = std::holds_alternative<int>(resolvedValue) ? std::get<int>(resolvedValue) : 0;
                    PushInt4(outBytes, v, 0, 0, 0);
                    break;
                }
                case ShaderParamType::Vec2: {
                    const ImVec2 v = std::holds_alternative<ImVec2>(resolvedValue) ? std::get<ImVec2>(resolvedValue) : ImVec2(0.0f, 0.0f);
                    PushFloat4(outBytes, v.x, v.y, 0.0f, 0.0f);
                    break;
                }
                case ShaderParamType::Vec3: {
                    const ImVec4 v = std::holds_alternative<ImVec4>(resolvedValue) ? std::get<ImVec4>(resolvedValue) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
                    PushFloat4(outBytes, v.x, v.y, v.z, 0.0f);
                    break;
                }
                case ShaderParamType::Vec4: {
                    const ImVec4 v = std::holds_alternative<ImVec4>(resolvedValue) ? std::get<ImVec4>(resolvedValue) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
                    PushFloat4(outBytes, v.x, v.y, v.z, v.w);
                    break;
                }
                case ShaderParamType::Color: {
                    const ImU32 c = std::holds_alternative<ImU32>(resolvedValue) ? std::get<ImU32>(resolvedValue) : IM_COL32(255, 255, 255, 255);
                    const float r = static_cast<float>((c >> 0) & 0xFF) / 255.0f;
                    const float g = static_cast<float>((c >> 8) & 0xFF) / 255.0f;
                    const float b = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
                    const float a = static_cast<float>((c >> 24) & 0xFF) / 255.0f;
                    PushFloat4(outBytes, r, g, b, a);
                    break;
                }
                case ShaderParamType::Bool: {
                    const bool b = std::holds_alternative<bool>(resolvedValue) ? std::get<bool>(resolvedValue) : false;
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

        void Add(const std::string& name, ShaderParamType type, const ShaderParamValue& value) {
            const std::string safeName = SanitizeName(name);
            for (auto& param : m_Params) {
                if (param.Name == safeName) {
                    param.Type = type;
                    param.Value = value;
                    param.BindingMode = ShaderBindingMode::Literal;
                    param.UniformKey.clear();
                    param.AutoUniform = ShaderAutoUniform::None;
                    return;
                }
            }

            ShaderParameter param;
            param.Name = safeName;
            param.Type = type;
            param.Value = value;
            param.BindingMode = ShaderBindingMode::Literal;
            param.UniformKey.clear();
            param.AutoUniform = ShaderAutoUniform::None;
            m_Params.push_back(std::move(param));
        }

        void AddAuto(
            const std::string& name,
            ShaderParamType type,
            const ShaderParamValue& fallbackValue,
            ShaderAutoUniform binding) {
            const std::string safeName = SanitizeName(name);
            for (auto& param : m_Params) {
                if (param.Name == safeName) {
                    param.Type = type;
                    param.Value = fallbackValue;
                    param.BindingMode = ShaderBindingMode::BuiltinAutoUniform;
                    param.UniformKey.clear();
                    param.AutoUniform = binding;
                    return;
                }
            }

            ShaderParameter param;
            param.Name = safeName;
            param.Type = type;
            param.Value = fallbackValue;
            param.BindingMode = ShaderBindingMode::BuiltinAutoUniform;
            param.UniformKey.clear();
            param.AutoUniform = binding;
            m_Params.push_back(std::move(param));
        }

        void AddBound(
            const std::string& name,
            ShaderParamType type,
            const ShaderParamValue& fallbackValue,
            const std::string& uniformKey) {
            const std::string safeName = SanitizeName(name);
            for (auto& param : m_Params) {
                if (param.Name == safeName) {
                    param.Type = type;
                    param.Value = fallbackValue;
                    param.BindingMode = ShaderBindingMode::RegisteredAutoUniform;
                    param.UniformKey = uniformKey;
                    param.AutoUniform = ShaderAutoUniform::None;
                    return;
                }
            }

            ShaderParameter param;
            param.Name = safeName;
            param.Type = type;
            param.Value = fallbackValue;
            param.BindingMode = ShaderBindingMode::RegisteredAutoUniform;
            param.UniformKey = uniformKey;
            param.AutoUniform = ShaderAutoUniform::None;
            m_Params.push_back(std::move(param));
        }

        static const char* AutoUniformToString(ShaderAutoUniform binding) {
            switch (binding) {
            case ShaderAutoUniform::None: return "None";
            case ShaderAutoUniform::TimeSeconds: return "TimeSeconds";
            case ShaderAutoUniform::DeltaSeconds: return "DeltaSeconds";
            case ShaderAutoUniform::MousePos: return "MousePos";
            case ShaderAutoUniform::DisplaySize: return "DisplaySize";
            case ShaderAutoUniform::EntityMin: return "EntityMin";
            case ShaderAutoUniform::EntityMax: return "EntityMax";
            case ShaderAutoUniform::EntitySize: return "EntitySize";
            case ShaderAutoUniform::EntityRect: return "EntityRect";
            }
            return "Unknown";
        }

        static bool IsValueCompatible(ShaderParamType type, const ShaderParamValue& value) {
            switch (type) {
            case ShaderParamType::Float:
                return std::holds_alternative<float>(value);
            case ShaderParamType::Int:
                return std::holds_alternative<int>(value);
            case ShaderParamType::Vec2:
                return std::holds_alternative<ImVec2>(value);
            case ShaderParamType::Vec3:
            case ShaderParamType::Vec4:
                return std::holds_alternative<ImVec4>(value);
            case ShaderParamType::Color:
                return std::holds_alternative<ImU32>(value);
            case ShaderParamType::Bool:
                return std::holds_alternative<bool>(value);
            }
            return false;
        }

        static ShaderParamValue ResolveBuiltinAutoUniform(
            const ShaderParameter& param,
            const ShaderAutoUniformContext* context,
            std::string* outResolveError) {
            if (param.AutoUniform == ShaderAutoUniform::None || context == nullptr) {
                return param.Value;
            }

            auto setMismatch = [&param, outResolveError]() {
                if (outResolveError && outResolveError->empty()) {
                    *outResolveError = "Auto uniform '" + std::string(AutoUniformToString(param.AutoUniform)) +
                        "' is incompatible with parameter '" + param.Name + "' type.";
                }
            };

            switch (param.AutoUniform) {
            case ShaderAutoUniform::None:
                return param.Value;
            case ShaderAutoUniform::TimeSeconds:
                if (param.Type == ShaderParamType::Float) return context->TimeSeconds;
                setMismatch();
                return param.Value;
            case ShaderAutoUniform::DeltaSeconds:
                if (param.Type == ShaderParamType::Float) return context->DeltaSeconds;
                setMismatch();
                return param.Value;
            case ShaderAutoUniform::MousePos:
                if (param.Type == ShaderParamType::Vec2) return context->MousePos;
                if (param.Type == ShaderParamType::Vec4) return ImVec4(context->MousePos.x, context->MousePos.y, 0.0f, 0.0f);
                setMismatch();
                return param.Value;
            case ShaderAutoUniform::DisplaySize:
                if (param.Type == ShaderParamType::Vec2) return context->DisplaySize;
                if (param.Type == ShaderParamType::Vec4) return ImVec4(context->DisplaySize.x, context->DisplaySize.y, 0.0f, 0.0f);
                setMismatch();
                return param.Value;
            case ShaderAutoUniform::EntityMin:
                if (param.Type == ShaderParamType::Vec2) return context->EntityMin;
                if (param.Type == ShaderParamType::Vec4) return ImVec4(context->EntityMin.x, context->EntityMin.y, 0.0f, 0.0f);
                setMismatch();
                return param.Value;
            case ShaderAutoUniform::EntityMax:
                if (param.Type == ShaderParamType::Vec2) return context->EntityMax;
                if (param.Type == ShaderParamType::Vec4) return ImVec4(context->EntityMax.x, context->EntityMax.y, 0.0f, 0.0f);
                setMismatch();
                return param.Value;
            case ShaderAutoUniform::EntitySize:
                if (param.Type == ShaderParamType::Vec2) return context->EntitySize;
                if (param.Type == ShaderParamType::Vec4) return ImVec4(context->EntitySize.x, context->EntitySize.y, 0.0f, 0.0f);
                setMismatch();
                return param.Value;
            case ShaderAutoUniform::EntityRect:
                if (param.Type == ShaderParamType::Vec4) return context->EntityRect;
                setMismatch();
                return param.Value;
            }
            return param.Value;
        }

        static ShaderParamValue ResolveRegisteredAutoUniform(
            const ShaderParameter& param,
            const ShaderAutoUniformContext* context,
            const RegisteredUniformResolver* registeredResolver,
            std::string* outResolveError) {
            if (context == nullptr || registeredResolver == nullptr) {
                return param.Value;
            }
            if (param.UniformKey.empty()) {
                if (outResolveError && outResolveError->empty()) {
                    *outResolveError = "Registered uniform key is empty for parameter '" + param.Name + "'.";
                }
                return param.Value;
            }

            ShaderParamValue resolved = param.Value;
            std::string resolveError;
            if (!(*registeredResolver)(param.UniformKey, *context, param.Type, resolved, resolveError)) {
                if (outResolveError && outResolveError->empty()) {
                    *outResolveError = resolveError.empty()
                        ? "Failed to resolve registered uniform '" + param.UniformKey + "' for parameter '" + param.Name + "'."
                        : resolveError;
                }
                return param.Value;
            }

            if (!IsValueCompatible(param.Type, resolved)) {
                if (outResolveError && outResolveError->empty()) {
                    *outResolveError =
                        "Registered uniform '" + param.UniformKey + "' produced incompatible type for parameter '" + param.Name + "'.";
                }
                return param.Value;
            }
            return resolved;
        }

        static ShaderParamValue ResolveAutoUniformValue(
            const ShaderParameter& param,
            const ShaderAutoUniformContext* context,
            const RegisteredUniformResolver* registeredResolver,
            std::string* outResolveError) {
            // Legacy compatibility: if callers set AutoUniform directly but did not set BindingMode.
            if (param.BindingMode == ShaderBindingMode::Literal && param.AutoUniform != ShaderAutoUniform::None) {
                return ResolveBuiltinAutoUniform(param, context, outResolveError);
            }

            switch (param.BindingMode) {
            case ShaderBindingMode::Literal:
                return param.Value;
            case ShaderBindingMode::BuiltinAutoUniform:
                return ResolveBuiltinAutoUniform(param, context, outResolveError);
            case ShaderBindingMode::RegisteredAutoUniform:
                return ResolveRegisteredAutoUniform(param, context, registeredResolver, outResolveError);
            }
            return param.Value;
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

        ShaderComponent& ConfigureParameters(const ShaderParamBuilder& builder) {
            Parameters = builder.Parameters();
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
