#include "FontSystem.hpp"

#include <algorithm>
#include <cfloat>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Dx12Init/Dx12Init.hpp"
#include "RustComponents/RustBridge.hpp"
#include "Components/FontsComponent.hpp"
#include "Components/ParentComponent.hpp"
#include "imgui_impl_dx12.h"

namespace RenderUtils {
    namespace {
        struct FaceRuntime {
            entt::entity Owner = entt::null;
            std::string RuntimeKey;
            std::string FaceKey;
            FontFaceSpec Spec;
            std::uint64_t SpecHash = 0;

            uint64_t FetchId = 0;
            int RetryCount = 0;
            double NextRetryAtSec = 0.0;
            bool TerminalFailure = false;
            std::string LastError;

            std::vector<std::uint8_t> Bytes;
            std::vector<std::uint8_t> PendingBytes;
            ImFont* AtlasFont = nullptr;
            std::uint64_t AtlasGeneration = 0;

            double LastFilePollSec = 0.0;
            bool HasLastWriteTime = false;
            std::filesystem::file_time_type LastWriteTime = {};

            ImVector<ImWchar> CachedGlyphRanges;
        };

        inline std::unordered_map<std::string, FaceRuntime> s_Faces;
        inline bool s_AtlasDirty = false;
        inline bool s_Registered = false;
        inline bool s_RebuildInProgress = false;
        inline std::uint64_t s_AtlasGeneration = 1;
        inline bool s_LastRebuildSucceeded = false;
        inline std::string s_LastRebuildMessage = "Font atlas has not been rebuilt yet.";

        template <typename T>
        inline void HashCombine(std::uint64_t& seed, const T& value) {
            const std::uint64_t hashValue = static_cast<std::uint64_t>(std::hash<T>{}(value));
            seed ^= hashValue + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        }

        inline std::uint64_t HashFaceSpec(const FontFaceSpec& spec) {
            std::uint64_t seed = 0xcbf29ce484222325ull;
            HashCombine(seed, spec.Key);
            HashCombine(seed, static_cast<int>(spec.SourceType));
            HashCombine(seed, spec.Source);
            HashCombine(seed, spec.SizePx);
            HashCombine(seed, spec.OversampleH);
            HashCombine(seed, spec.OversampleV);
            HashCombine(seed, spec.PixelSnapH);
            HashCombine(seed, spec.MergeMode);
            HashCombine(seed, spec.RasterizerMultiply);
            HashCombine(seed, static_cast<int>(spec.GlyphPreset));
            HashCombine(seed, spec.ExtraGlyphs);
            HashCombine(seed, spec.MaxBytes);
            HashCombine(seed, spec.MaxRetryCount);
            HashCombine(seed, spec.RetryDelayMs);
            HashCombine(seed, spec.StartupRequired);
            return seed;
        }

        inline double NowSec() {
            if (!ImGui::GetCurrentContext()) {
                return 0.0;
            }
            return ImGui::GetTime();
        }

        inline std::string BuildRuntimeKey(entt::entity owner, const std::string& faceKey) {
            return std::to_string(static_cast<std::uint32_t>(entt::to_integral(owner))) + "::" + faceKey;
        }

        inline bool StartsWith(const std::string& value, const char* prefix) {
            return value.rfind(prefix, 0) == 0;
        }

        inline bool IsHttpsUrl(const std::string& value) {
            return StartsWith(value, "https://");
        }

        inline bool TryReadWriteTime(const std::string& path, std::filesystem::file_time_type& outTime) {
            std::error_code ec;
            const std::filesystem::path fsPath(path);
            if (!std::filesystem::exists(fsPath, ec) || ec) {
                return false;
            }
            const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(fsPath, ec);
            if (ec) {
                return false;
            }
            outTime = writeTime;
            return true;
        }

        inline void CancelFetch(FaceRuntime& runtime) {
            if (runtime.FetchId != 0) {
                cancel_fetch_request(runtime.FetchId);
                runtime.FetchId = 0;
            }
        }

        inline void ScheduleRetry(FaceRuntime& runtime, const char* errorMessage, bool terminal) {
            runtime.LastError = errorMessage ? errorMessage : "font fetch failed";
            CancelFetch(runtime);

            if (terminal) {
                runtime.TerminalFailure = runtime.Bytes.empty() && runtime.PendingBytes.empty();
                runtime.NextRetryAtSec = std::numeric_limits<double>::infinity();
                return;
            }

            if (runtime.RetryCount < runtime.Spec.MaxRetryCount) {
                ++runtime.RetryCount;
                const double retryDelaySec = static_cast<double>((std::max)(0.0f, runtime.Spec.RetryDelayMs)) / 1000.0;
                runtime.NextRetryAtSec = NowSec() + retryDelaySec;
                runtime.TerminalFailure = false;
            } else {
                runtime.TerminalFailure = runtime.Bytes.empty() && runtime.PendingBytes.empty();
                runtime.NextRetryAtSec = std::numeric_limits<double>::infinity();
            }
        }

        inline bool StartFetch(FaceRuntime& runtime) {
            CancelFetch(runtime);
            runtime.LastError.clear();
            runtime.TerminalFailure = false;

            if (runtime.Spec.Source.empty()) {
                ScheduleRetry(runtime, "font source is empty", true);
                return false;
            }

            if (runtime.Spec.SourceType == FontSourceType::Url && !IsHttpsUrl(runtime.Spec.Source)) {
                ScheduleRetry(runtime, "font URL must use HTTPS", true);
                return false;
            }

            const int32_t sourceKind = runtime.Spec.SourceType == FontSourceType::Url
                ? static_cast<int32_t>(ImageSourceKind::Url)
                : static_cast<int32_t>(ImageSourceKind::LocalPath);
            const uint64_t fetchId = start_fetch_bytes(runtime.Spec.Source.c_str(), sourceKind);
            if (fetchId == 0) {
                ScheduleRetry(runtime, "failed to start font fetch", false);
                return false;
            }

            runtime.FetchId = fetchId;
            return true;
        }

        inline void PollFetch(FaceRuntime& runtime) {
            if (runtime.FetchId == 0) {
                return;
            }

            std::uint8_t* fetchedData = nullptr;
            std::size_t fetchedLen = 0;
            const int32_t status = check_fetch_bytes_status_ex(runtime.FetchId, &fetchedData, &fetchedLen);
            if (status == static_cast<int32_t>(ImageFetchStatus::Loading)) {
                return;
            }

            CancelFetch(runtime);
            if (status == static_cast<int32_t>(ImageFetchStatus::Ready)) {
                if (fetchedData == nullptr || fetchedLen == 0) {
                    ScheduleRetry(runtime, "font payload was empty", false);
                    return;
                }
                if (fetchedLen > runtime.Spec.MaxBytes) {
                    free_rust_bytes(fetchedData, fetchedLen);
                    ScheduleRetry(runtime, "font payload exceeds max byte limit", true);
                    return;
                }

                runtime.PendingBytes.assign(fetchedData, fetchedData + fetchedLen);
                free_rust_bytes(fetchedData, fetchedLen);
                runtime.RetryCount = 0;
                runtime.NextRetryAtSec = 0.0;
                runtime.TerminalFailure = false;
                runtime.LastError.clear();
                s_AtlasDirty = true;
                return;
            }

            ScheduleRetry(runtime, "font fetch failed", status == static_cast<int32_t>(ImageFetchStatus::InvalidId));
        }

        inline const ImWchar* GetGlyphPresetRanges(ImFontAtlas* atlas, FontGlyphPreset preset) {
            if (!atlas) {
                return nullptr;
            }

            switch (preset) {
            case FontGlyphPreset::Cyrillic:
                return atlas->GetGlyphRangesCyrillic();
            case FontGlyphPreset::Japanese:
                return atlas->GetGlyphRangesJapanese();
            case FontGlyphPreset::Korean:
                return atlas->GetGlyphRangesKorean();
            case FontGlyphPreset::ChineseFull:
                return atlas->GetGlyphRangesChineseFull();
            case FontGlyphPreset::Default:
            default:
                return atlas->GetGlyphRangesDefault();
            }
        }

        inline const ImWchar* BuildGlyphRanges(FaceRuntime& runtime, ImFontAtlas* atlas) {
            const ImWchar* baseRanges = GetGlyphPresetRanges(atlas, runtime.Spec.GlyphPreset);
            if (runtime.Spec.ExtraGlyphs.empty()) {
                runtime.CachedGlyphRanges.clear();
                return baseRanges;
            }

            ImFontGlyphRangesBuilder builder;
            if (baseRanges) {
                builder.AddRanges(baseRanges);
            }
            builder.AddText(runtime.Spec.ExtraGlyphs.c_str());
            runtime.CachedGlyphRanges.clear();
            builder.BuildRanges(&runtime.CachedGlyphRanges);
            return runtime.CachedGlyphRanges.empty() ? baseRanges : runtime.CachedGlyphRanges.Data;
        }

        inline bool TryAddFontPayload(FaceRuntime& runtime, ImFontAtlas* atlas, const std::vector<std::uint8_t>& bytes, ImFont*& outFont) {
            outFont = nullptr;
            if (!atlas || bytes.empty()) {
                return false;
            }

            const std::size_t clampedLen = (std::min)(bytes.size(), static_cast<std::size_t>((std::numeric_limits<int>::max)()));
            const int maxLen = static_cast<int>(clampedLen);
            if (maxLen <= 0) {
                return false;
            }

            ImFontConfig cfg;
            cfg.OversampleH = (std::max)(1, runtime.Spec.OversampleH);
            cfg.OversampleV = (std::max)(1, runtime.Spec.OversampleV);
            cfg.PixelSnapH = runtime.Spec.PixelSnapH;
            cfg.MergeMode = runtime.Spec.MergeMode;
            cfg.RasterizerMultiply = runtime.Spec.RasterizerMultiply;
            cfg.FontDataOwnedByAtlas = false;

            const float safeSizePx = runtime.Spec.SizePx < 4.0f ? 4.0f : runtime.Spec.SizePx;
            const ImWchar* ranges = BuildGlyphRanges(runtime, atlas);
            outFont = atlas->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(bytes.data()), maxLen, safeSizePx, &cfg, ranges);
            return outFont != nullptr;
        }

        inline void RebuildAtlas() {
            if (!s_AtlasDirty) {
                return;
            }
            if (s_RebuildInProgress) {
                return;
            }
            if (!ImGui::GetCurrentContext()) {
                s_LastRebuildSucceeded = false;
                s_LastRebuildMessage = "Deferred font atlas rebuild: ImGui context unavailable.";
                return;
            }
            if (!DX12Init::g_pd3dDevice || !DX12Init::g_pd3dSrvDescHeap) {
                s_LastRebuildSucceeded = false;
                s_LastRebuildMessage = "Deferred font atlas rebuild: DX12 device/heap unavailable.";
                return;
            }

            s_RebuildInProgress = true;
            struct RebuildGuard {
                bool& Flag;
                ~RebuildGuard() { Flag = false; }
            } guard{ s_RebuildInProgress };

            DX12Init::WaitForLastSubmittedFrame();
            ImGui_ImplDX12_InvalidateDeviceObjects();

            ImGuiIO& io = ImGui::GetIO();
            if (!io.Fonts) {
                s_LastRebuildSucceeded = false;
                s_LastRebuildMessage = "Font atlas rebuild failed: ImGuiIO::Fonts is null.";
                return;
            }

            io.Fonts->Clear();
            io.FontDefault = io.Fonts->AddFontDefault();

            std::vector<FaceRuntime*> orderedFaces;
            orderedFaces.reserve(s_Faces.size());
            for (auto& entry : s_Faces) {
                entry.second.AtlasFont = nullptr;
                entry.second.AtlasGeneration = 0;
                if (!entry.second.Bytes.empty() || !entry.second.PendingBytes.empty()) {
                    orderedFaces.push_back(&entry.second);
                }
            }

            std::sort(orderedFaces.begin(), orderedFaces.end(), [](const FaceRuntime* lhs, const FaceRuntime* rhs) {
                return lhs->RuntimeKey < rhs->RuntimeKey;
            });

            const std::uint64_t pendingGeneration = s_AtlasGeneration + 1;
            for (FaceRuntime* runtime : orderedFaces) {
                if (!runtime) {
                    continue;
                }

                ImFont* font = nullptr;
                if (!runtime->PendingBytes.empty()) {
                    std::vector<std::uint8_t> previousBytes = std::move(runtime->Bytes);
                    runtime->Bytes = std::move(runtime->PendingBytes);
                    runtime->PendingBytes.clear();

                    if (!TryAddFontPayload(*runtime, io.Fonts, runtime->Bytes, font)) {
                        runtime->LastError = "font atlas rejected fetched payload";
                        runtime->Bytes.clear();
                        if (!previousBytes.empty() && TryAddFontPayload(*runtime, io.Fonts, previousBytes, font)) {
                            runtime->Bytes = std::move(previousBytes);
                            runtime->LastError.clear();
                        } else {
                            previousBytes.clear();
                        }
                    }
                } else if (!runtime->Bytes.empty()) {
                    if (!TryAddFontPayload(*runtime, io.Fonts, runtime->Bytes, font)) {
                        runtime->LastError = "font atlas rejected cached payload";
                        runtime->Bytes.clear();
                    }
                }

                runtime->AtlasFont = font;
                runtime->AtlasGeneration = font ? pendingGeneration : 0;
                if (!font && runtime->Bytes.empty() && runtime->PendingBytes.empty()) {
                    runtime->TerminalFailure = true;
                }
            }

            const bool built = io.Fonts->Build();
            if (!built) {
                s_LastRebuildSucceeded = false;
                s_LastRebuildMessage = "Font atlas rebuild failed: ImGui font build returned false.";
                return;
            }

            const bool created = ImGui_ImplDX12_CreateDeviceObjects();
            if (!created) {
                for (auto& entry : s_Faces) {
                    entry.second.AtlasFont = nullptr;
                    entry.second.AtlasGeneration = 0;
                }
                s_LastRebuildSucceeded = false;
                s_LastRebuildMessage = "Font atlas rebuild failed: ImGui DX12 device objects creation failed.";
                return;
            }

            s_AtlasGeneration = pendingGeneration;
            s_AtlasDirty = false;
            s_LastRebuildSucceeded = true;
            s_LastRebuildMessage = "Font atlas rebuilt successfully at pre-NewFrame boundary.";
        }

        inline std::vector<entt::entity> CollectFontOwners(entt::registry& registry, entt::entity entity) {
            std::vector<entt::entity> owners;
            entt::entity current = entity;
            while (registry.valid(current)) {
                const auto* fonts = registry.try_get<FontsComponent>(current);
                if (fonts && fonts->Enabled) {
                    owners.push_back(current);
                    if (!fonts->InheritFromParent) {
                        break;
                    }
                }

                const auto* parent = registry.try_get<ParentComponent>(current);
                if (!parent || !registry.valid(parent->ParentEntity)) {
                    break;
                }
                current = parent->ParentEntity;
            }

            return owners;
        }

        inline bool FaceIsReadyForUse(const FaceRuntime& runtime) {
            return runtime.AtlasFont != nullptr &&
                   !runtime.Bytes.empty() &&
                   runtime.AtlasGeneration == s_AtlasGeneration;
        }

        inline FaceRuntime* FindFace(entt::entity owner, const std::string& key) {
            if (key.empty()) {
                return nullptr;
            }
            const std::string runtimeKey = BuildRuntimeKey(owner, key);
            auto it = s_Faces.find(runtimeKey);
            if (it == s_Faces.end()) {
                return nullptr;
            }
            return &it->second;
        }

        inline FontFaceRuntimeState QueryFaceRuntimeState(entt::entity owner, const std::string& key) {
            FaceRuntime* runtime = FindFace(owner, key);
            if (!runtime) {
                return FontFaceRuntimeState::Missing;
            }
            if (FaceIsReadyForUse(*runtime)) {
                return FontFaceRuntimeState::Ready;
            }
            if (runtime->TerminalFailure) {
                return FontFaceRuntimeState::Failed;
            }
            return FontFaceRuntimeState::Loading;
        }
    }

    void FontSystem::Register(entt::registry& registry) {
        (void)registry;
        s_Registered = true;
    }

    void FontSystem::Shutdown(entt::registry& registry) {
        (void)registry;
        for (auto& entry : s_Faces) {
            CancelFetch(entry.second);
        }
        s_Faces.clear();
        s_AtlasDirty = false;
        s_RebuildInProgress = false;
        s_AtlasGeneration = 1;
        s_LastRebuildSucceeded = false;
        s_LastRebuildMessage = "Font system shut down.";
        s_Registered = false;
    }

    void FontSystem::Update(entt::registry& registry) {
        if (!s_Registered || !ImGui::GetCurrentContext()) {
            return;
        }

        const double nowSec = NowSec();
        std::unordered_set<std::string> activeKeys;

        auto view = registry.view<FontsComponent>();
        for (auto entity : view) {
            auto& fonts = view.get<FontsComponent>(entity);
            if (!fonts.Enabled) {
                fonts.ReloadRequested = false;
                continue;
            }

            const float reloadPollSec = fonts.ReloadPollSeconds < 0.05f ? 0.05f : fonts.ReloadPollSeconds;
            for (const auto& face : fonts.Faces) {
                if (face.Key.empty() || face.Source.empty()) {
                    continue;
                }

                const std::string runtimeKey = BuildRuntimeKey(entity, face.Key);
                activeKeys.insert(runtimeKey);

                auto [it, inserted] = s_Faces.try_emplace(runtimeKey);
                FaceRuntime& runtime = it->second;

                if (inserted) {
                    runtime.Owner = entity;
                    runtime.RuntimeKey = runtimeKey;
                    runtime.FaceKey = face.Key;
                    runtime.Spec = face;
                    runtime.SpecHash = HashFaceSpec(face);
                    runtime.NextRetryAtSec = 0.0;
                } else {
                    const std::uint64_t nextHash = HashFaceSpec(face);
                    if (runtime.Owner != entity || runtime.SpecHash != nextHash) {
                        CancelFetch(runtime);
                        runtime.Owner = entity;
                        runtime.FaceKey = face.Key;
                        runtime.Spec = face;
                        runtime.SpecHash = nextHash;
                        runtime.RetryCount = 0;
                        runtime.NextRetryAtSec = 0.0;
                        runtime.TerminalFailure = false;
                        runtime.LastError.clear();
                        runtime.PendingBytes.clear();
                        runtime.CachedGlyphRanges.clear();
                        s_AtlasDirty = true;
                    }
                }

                bool forceReload = fonts.ReloadRequested;
                if (!forceReload && face.SourceType == FontSourceType::LocalPath && fonts.AutoReloadLocalFiles) {
                    if (runtime.LastFilePollSec <= 0.0 || (nowSec - runtime.LastFilePollSec) >= static_cast<double>(reloadPollSec)) {
                        runtime.LastFilePollSec = nowSec;
                        std::filesystem::file_time_type writeTime = {};
                        const bool haveWriteTime = TryReadWriteTime(face.Source, writeTime);
                        if (haveWriteTime) {
                            if (!runtime.HasLastWriteTime) {
                                runtime.HasLastWriteTime = true;
                                runtime.LastWriteTime = writeTime;
                            } else if (writeTime != runtime.LastWriteTime) {
                                runtime.LastWriteTime = writeTime;
                                forceReload = true;
                            }
                        }
                    }
                }

                if (forceReload) {
                    runtime.RetryCount = 0;
                    runtime.NextRetryAtSec = 0.0;
                    runtime.TerminalFailure = false;
                    StartFetch(runtime);
                } else if (runtime.FetchId != 0) {
                    PollFetch(runtime);
                } else if (runtime.Bytes.empty() && runtime.PendingBytes.empty() && !runtime.TerminalFailure && nowSec >= runtime.NextRetryAtSec) {
                    StartFetch(runtime);
                }
            }

            fonts.ReloadRequested = false;
        }

        for (auto it = s_Faces.begin(); it != s_Faces.end();) {
            if (activeKeys.find(it->first) != activeKeys.end()) {
                ++it;
                continue;
            }

            const bool hadAtlasContent = it->second.AtlasFont != nullptr || !it->second.Bytes.empty() || !it->second.PendingBytes.empty();
            CancelFetch(it->second);
            it = s_Faces.erase(it);
            if (hadAtlasContent) {
                s_AtlasDirty = true;
            }
        }
    }

    void FontSystem::ProcessPendingAtlasRebuild() {
        if (!s_Registered) {
            return;
        }
        RebuildAtlas();
    }

    FontLoadProgress FontSystem::QueryProgress(entt::registry& registry) {
        FontLoadProgress progress;
        progress.done = true;
        progress.percent = 1.0f;

        auto view = registry.view<FontsComponent>();
        for (auto entity : view) {
            const auto& fonts = view.get<FontsComponent>(entity);
            if (!fonts.Enabled) {
                continue;
            }

            for (const auto& face : fonts.Faces) {
                if (!face.StartupRequired || face.Key.empty() || face.Source.empty()) {
                    continue;
                }

                ++progress.totalRequired;

                const std::string runtimeKey = BuildRuntimeKey(entity, face.Key);
                auto it = s_Faces.find(runtimeKey);
                if (it == s_Faces.end()) {
                    ++progress.loadingRequired;
                    if (progress.currentLabel.empty()) {
                        progress.currentLabel = "Loading required font: " + face.Key;
                    }
                    continue;
                }

                const FaceRuntime& runtime = it->second;
                if (FaceIsReadyForUse(runtime)) {
                    ++progress.readyRequired;
                    continue;
                }

                if (runtime.TerminalFailure) {
                    ++progress.failedRequired;
                    if (progress.currentLabel.empty()) {
                        progress.currentLabel = "Failed required font: " + face.Key;
                    }
                    continue;
                }

                ++progress.loadingRequired;
                if (progress.currentLabel.empty()) {
                    progress.currentLabel = runtime.LastError.empty()
                        ? ("Loading required font: " + face.Key)
                        : runtime.LastError;
                }
            }
        }

        if (progress.totalRequired <= 0) {
            return progress;
        }

        const int completed = progress.readyRequired + progress.failedRequired;
        progress.percent = static_cast<float>(completed) / static_cast<float>(progress.totalRequired);
        if (progress.percent < 0.0f) {
            progress.percent = 0.0f;
        }
        if (progress.percent > 1.0f) {
            progress.percent = 1.0f;
        }
        progress.done = progress.loadingRequired <= 0;
        return progress;
    }

    std::vector<FontFaceRuntimeInfo> FontSystem::QueryEntityFontFaces(
        entt::registry& registry,
        entt::entity entity,
        bool includeInherited) {
        std::vector<FontFaceRuntimeInfo> faces;
        if (!registry.valid(entity)) {
            return faces;
        }

        const std::vector<entt::entity> owners = CollectFontOwners(registry, entity);
        if (owners.empty()) {
            return faces;
        }

        std::string effectiveDefaultKey;
        for (entt::entity owner : owners) {
            const auto* fonts = registry.try_get<FontsComponent>(owner);
            if (!fonts || !fonts->Enabled || fonts->DefaultFaceKey.empty()) {
                continue;
            }
            effectiveDefaultKey = fonts->DefaultFaceKey;
            break;
        }

        std::unordered_set<std::string> seenKeys;
        for (size_t ownerIndex = 0; ownerIndex < owners.size(); ++ownerIndex) {
            if (ownerIndex > 0 && !includeInherited) {
                break;
            }

            const entt::entity owner = owners[ownerIndex];
            const auto* fonts = registry.try_get<FontsComponent>(owner);
            if (!fonts || !fonts->Enabled) {
                continue;
            }

            for (const auto& face : fonts->Faces) {
                if (face.Key.empty()) {
                    continue;
                }
                if (seenKeys.find(face.Key) != seenKeys.end()) {
                    continue;
                }
                seenKeys.insert(face.Key);

                FontFaceRuntimeInfo info;
                info.Key = face.Key;
                info.State = QueryFaceRuntimeState(owner, face.Key);
                info.Inherited = ownerIndex > 0;
                info.IsDefault = !effectiveDefaultKey.empty() && face.Key == effectiveDefaultKey;
                faces.push_back(std::move(info));
            }
        }

        return faces;
    }

    bool FontSystem::SetEntityDefaultFace(
        entt::registry& registry,
        entt::entity entity,
        const std::string& key) {
        if (!registry.valid(entity)) {
            return false;
        }
        auto* fonts = registry.try_get<FontsComponent>(entity);
        if (!fonts) {
            return false;
        }

        if (key.empty()) {
            fonts->DefaultFaceKey.clear();
            return true;
        }

        const std::vector<FontFaceRuntimeInfo> availableFaces = QueryEntityFontFaces(registry, entity, true);
        const bool faceExists = (std::find_if)(
            availableFaces.begin(),
            availableFaces.end(),
            [&key](const FontFaceRuntimeInfo& info) {
                return info.Key == key;
            }) != availableFaces.end();
        if (!faceExists) {
            return false;
        }

        fonts->DefaultFaceKey = key;
        return true;
    }

    bool FontSystem::ShouldApply(entt::registry& registry, entt::entity entity, FontApplyTarget target) {
        const std::vector<entt::entity> owners = CollectFontOwners(registry, entity);
        if (owners.empty()) {
            return false;
        }

        const FontsComponent* nearest = registry.try_get<FontsComponent>(owners.front());
        if (!nearest || !nearest->Enabled) {
            return false;
        }

        switch (target) {
        case FontApplyTarget::Text:
            return nearest->ApplyToText;
        case FontApplyTarget::TextInput:
            return nearest->ApplyToTextInput;
        case FontApplyTarget::Options:
            return nearest->ApplyToOptions;
        case FontApplyTarget::StatusLabel:
            return nearest->ApplyToStatusLabels;
        default:
            return false;
        }
    }

    ImFont* FontSystem::ResolveEntityFont(entt::registry& registry, entt::entity entity, const std::string& requestedKey) {
        const std::vector<entt::entity> owners = CollectFontOwners(registry, entity);
        if (owners.empty()) {
            return nullptr;
        }

        if (!requestedKey.empty()) {
            for (entt::entity owner : owners) {
                FaceRuntime* face = FindFace(owner, requestedKey);
                if (face && FaceIsReadyForUse(*face)) {
                    return face->AtlasFont;
                }
            }
        }

        for (entt::entity owner : owners) {
            const auto* fonts = registry.try_get<FontsComponent>(owner);
            if (!fonts || !fonts->Enabled || fonts->DefaultFaceKey.empty()) {
                continue;
            }
            FaceRuntime* face = FindFace(owner, fonts->DefaultFaceKey);
            if (face && FaceIsReadyForUse(*face)) {
                return face->AtlasFont;
            }
        }

        return nullptr;
    }

    ImVec2 FontSystem::CalcTextSize(ImFont* font, const char* text) {
        if (!text || text[0] == '\0') {
            return ImVec2(0.0f, 0.0f);
        }

        if (!font) {
            return ImGui::CalcTextSize(text);
        }

        const char* textEnd = text + std::strlen(text);
        ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, -1.0f, text, textEnd, nullptr);
        if (size.y <= 0.0f) {
            size.y = font->FontSize;
        }
        return size;
    }

    ImVec2 FontSystem::CalcTextSize(ImFont* font, const std::string& text) {
        return CalcTextSize(font, text.c_str());
    }

    void FontSystem::AddText(
        ImDrawList* drawList,
        ImFont* font,
        const ImVec2& pos,
        ImU32 color,
        const char* text,
        float scale) {
        if (!drawList || !text || text[0] == '\0') {
            return;
        }

        const float safeScale = scale <= 0.0f ? 1.0f : scale;
        if (!font) {
            drawList->AddText(pos, color, text);
            return;
        }

        drawList->AddText(font, font->FontSize * safeScale, pos, color, text);
    }
}
