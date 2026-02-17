#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace RenderUtils {
    enum class FontSourceType {
        LocalPath,
        Url
    };

    enum class FontGlyphPreset {
        Default,
        Cyrillic,
        Japanese,
        Korean,
        ChineseFull
    };

    struct FontFaceSpec {
        std::string Key;
        FontSourceType SourceType = FontSourceType::LocalPath;
        std::string Source;
        float SizePx = 18.0f;
        int OversampleH = 2;
        int OversampleV = 2;
        bool PixelSnapH = false;
        bool MergeMode = false;
        float RasterizerMultiply = 1.0f;
        FontGlyphPreset GlyphPreset = FontGlyphPreset::Default;
        std::string ExtraGlyphs;
        std::size_t MaxBytes = 8u * 1024u * 1024u;
        int MaxRetryCount = 2;
        float RetryDelayMs = 350.0f;
        bool StartupRequired = false;

        FontFaceSpec() = default;

        FontFaceSpec& SetGlyphPreset(FontGlyphPreset preset) {
            GlyphPreset = preset;
            return *this;
        }

        FontFaceSpec& SetOversample(int h, int v) {
            OversampleH = (std::max)(1, h);
            OversampleV = (std::max)(1, v);
            return *this;
        }

        FontFaceSpec& SetPixelSnapH(bool enabled) {
            PixelSnapH = enabled;
            return *this;
        }

        FontFaceSpec& SetMergeMode(bool merge) {
            MergeMode = merge;
            return *this;
        }

        FontFaceSpec& SetRasterizerMultiply(float value) {
            if (value < 0.1f) {
                value = 0.1f;
            }
            if (value > 4.0f) {
                value = 4.0f;
            }
            RasterizerMultiply = value;
            return *this;
        }

        FontFaceSpec& SetExtraGlyphs(const std::string& glyphs) {
            ExtraGlyphs = glyphs;
            return *this;
        }

        FontFaceSpec& SetMaxBytes(std::size_t bytes) {
            const std::size_t minBytes = 1024u;
            MaxBytes = (std::max)(minBytes, bytes);
            return *this;
        }

        FontFaceSpec& SetRetryPolicy(int maxRetryCount, float retryDelayMs) {
            MaxRetryCount = (std::max)(0, maxRetryCount);
            RetryDelayMs = retryDelayMs < 0.0f ? 0.0f : retryDelayMs;
            return *this;
        }

        FontFaceSpec& SetStartupRequired(bool required) {
            StartupRequired = required;
            return *this;
        }
    };

    struct FontsComponent {
        bool Enabled = true;
        bool InheritFromParent = true;
        std::string DefaultFaceKey;
        bool ApplyToText = true;
        bool ApplyToTextInput = true;
        bool ApplyToOptions = true;
        bool ApplyToStatusLabels = true;
        bool AutoReloadLocalFiles = false;
        float ReloadPollSeconds = 1.0f;
        bool ReloadRequested = false;
        std::vector<FontFaceSpec> Faces;

        FontsComponent& SetEnabled(bool enabled) {
            Enabled = enabled;
            return *this;
        }

        FontsComponent& SetDefaultFace(const std::string& key) {
            DefaultFaceKey = key;
            return *this;
        }

        bool SetDefaultFaceSafe(const std::string& key) {
            if (key.empty()) {
                DefaultFaceKey.clear();
                return true;
            }
            if (!HasFace(key)) {
                return false;
            }
            DefaultFaceKey = key;
            return true;
        }

        FontsComponent& SetInheritFromParent(bool inherit) {
            InheritFromParent = inherit;
            return *this;
        }

        FontsComponent& SetApplyToText(bool enabled) {
            ApplyToText = enabled;
            return *this;
        }

        FontsComponent& SetApplyToTextInput(bool enabled) {
            ApplyToTextInput = enabled;
            return *this;
        }

        FontsComponent& SetApplyToOptions(bool enabled) {
            ApplyToOptions = enabled;
            return *this;
        }

        FontsComponent& SetApplyToStatusLabels(bool enabled) {
            ApplyToStatusLabels = enabled;
            return *this;
        }

        FontsComponent& SetAutoReloadLocalFiles(bool enabled) {
            AutoReloadLocalFiles = enabled;
            return *this;
        }

        FontsComponent& SetReloadPollSeconds(float seconds) {
            ReloadPollSeconds = seconds < 0.05f ? 0.05f : seconds;
            return *this;
        }

        FontsComponent& RequestReload() {
            ReloadRequested = true;
            return *this;
        }

        FontFaceSpec& AddPathFace(const std::string& key, const std::string& path, float sizePx) {
            FontFaceSpec face;
            face.Key = key;
            face.SourceType = FontSourceType::LocalPath;
            face.Source = path;
            face.SizePx = sizePx > 4.0f ? sizePx : 4.0f;
            Faces.push_back(std::move(face));
            return Faces.back();
        }

        FontFaceSpec& AddUrlFace(const std::string& key, const std::string& httpsUrl, float sizePx) {
            FontFaceSpec face;
            face.Key = key;
            face.SourceType = FontSourceType::Url;
            face.Source = httpsUrl;
            face.SizePx = sizePx > 4.0f ? sizePx : 4.0f;
            Faces.push_back(std::move(face));
            return Faces.back();
        }

        FontFaceSpec* FindFace(const std::string& key) {
            for (auto& face : Faces) {
                if (face.Key == key) {
                    return &face;
                }
            }
            return nullptr;
        }

        const FontFaceSpec* FindFace(const std::string& key) const {
            for (const auto& face : Faces) {
                if (face.Key == key) {
                    return &face;
                }
            }
            return nullptr;
        }

        std::vector<std::string> GetFaceKeys() const {
            std::vector<std::string> keys;
            keys.reserve(Faces.size());
            for (const auto& face : Faces) {
                if (face.Key.empty()) {
                    continue;
                }
                if ((std::find)(keys.begin(), keys.end(), face.Key) == keys.end()) {
                    keys.push_back(face.Key);
                }
            }
            return keys;
        }

        int FindFaceIndex(const std::string& key) const {
            if (key.empty()) {
                return -1;
            }
            for (size_t i = 0; i < Faces.size(); ++i) {
                if (Faces[i].Key == key) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        bool HasFace(const std::string& key) const {
            return FindFaceIndex(key) >= 0;
        }
    };
}
