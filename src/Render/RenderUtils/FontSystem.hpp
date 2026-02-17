#pragma once

#include <string>
#include <vector>

#include "entt/entt.hpp"
#include "imgui.h"

namespace RenderUtils {
    enum class FontApplyTarget {
        Text,
        TextInput,
        Options,
        StatusLabel
    };

    struct FontLoadProgress {
        int totalRequired = 0;
        int readyRequired = 0;
        int failedRequired = 0;
        int loadingRequired = 0;
        bool done = true;
        std::string currentLabel;
        float percent = 1.0f;
    };

    enum class FontFaceRuntimeState {
        Missing,
        Loading,
        Ready,
        Failed
    };

    struct FontFaceRuntimeInfo {
        std::string Key;
        FontFaceRuntimeState State = FontFaceRuntimeState::Missing;
        bool Inherited = false;
        bool IsDefault = false;
    };

    class FontSystem {
    public:
        static void Register(entt::registry& registry);
        static void Shutdown(entt::registry& registry);
        static void Update(entt::registry& registry);
        static void ProcessPendingAtlasRebuild();

        static FontLoadProgress QueryProgress(entt::registry& registry);
        static std::vector<FontFaceRuntimeInfo> QueryEntityFontFaces(
            entt::registry& registry,
            entt::entity entity,
            bool includeInherited = true);
        static bool SetEntityDefaultFace(
            entt::registry& registry,
            entt::entity entity,
            const std::string& key);

        static bool ShouldApply(entt::registry& registry, entt::entity entity, FontApplyTarget target);
        static ImFont* ResolveEntityFont(entt::registry& registry, entt::entity entity, const std::string& requestedKey = "");

        static ImVec2 CalcTextSize(ImFont* font, const char* text);
        static ImVec2 CalcTextSize(ImFont* font, const std::string& text);
        static void AddText(
            ImDrawList* drawList,
            ImFont* font,
            const ImVec2& pos,
            ImU32 color,
            const char* text,
            float scale = 1.0f);
    };
}
