#pragma once
#include "imgui.h"
#include "ContainerComponent.hpp"
#include "TransformComponent.hpp"
#include <cfloat>
#include <functional>
#include <string>
#include <vector>

namespace RenderUtils {

    enum class TextAlign {
        Left,
        Center,
        Right,
        Justify // Like Google Docs "Justify"
    };

    enum class TextFlags : int {
        None = 0,
        Wrap = 1 << 0,
        Clip = 1 << 1,
        PrecisionMode = 1 << 2
    };

    // Bitwise operators for TextFlags
    inline TextFlags operator|(TextFlags a, TextFlags b) { return static_cast<TextFlags>(static_cast<int>(a) | static_cast<int>(b)); }
    inline TextFlags operator&(TextFlags a, TextFlags b) { return static_cast<TextFlags>(static_cast<int>(a) & static_cast<int>(b)); }
    inline bool HasFlag(TextFlags flags, TextFlags check) { return (static_cast<int>(flags) & static_cast<int>(check)) != 0; }

    enum class TextStyle : int {
        Normal = 0,
        Bold = 1 << 0,
        Italic = 1 << 1 // For future expansion
    };
    inline TextStyle operator|(TextStyle a, TextStyle b) { return static_cast<TextStyle>(static_cast<int>(a) | static_cast<int>(b)); }
    inline bool HasStyle(TextStyle style, TextStyle check) { return (static_cast<int>(style) & static_cast<int>(check)) != 0; }


    struct TextSpan {
        std::string Text;
        ImU32 Color;
        TextStyle Style;
        std::string FontKey;
        
        TextSpan(const std::string& text, ImU32 color, TextStyle style = TextStyle::Normal, const std::string& fontKey = "")
            : Text(text), Color(color), Style(style), FontKey(fontKey) {}
    };

    // Rich text content
    struct TextComponent {
        std::string RawText; // For simple usage
        std::vector<TextSpan> Spans; // For rich text
        ImU32 Color; // Default color
        TextAlign Alignment;
        TextFlags Flags;
        float LineHeight;
        std::string FontKey;

        // Simple Constructor
        TextComponent(const char* text = "", ImU32 color = IM_COL32(255,255,255,255), TextFlags flags = TextFlags::None, TextAlign align = TextAlign::Left)
            : RawText(text), Color(color), Alignment(align), Flags(flags), LineHeight(1.0f) {}

        // --- Fluent Builder API ---

        // Add a rich text span (Chainable)
        TextComponent& Span(const std::string& text, ImU32 color, TextStyle style = TextStyle::Normal) {
            Spans.emplace_back(text, color, style);
            return *this;
        }

        TextComponent& Span(const std::string& text, ImU32 color, const std::string& fontKey) {
            Spans.emplace_back(text, color, TextStyle::Normal, fontKey);
            return *this;
        }

        // Add a span with default color (Chainable)
        TextComponent& Span(const std::string& text, TextStyle style = TextStyle::Normal) {
            Spans.emplace_back(text, Color, style);
            return *this;
        }

        TextComponent& Span(const std::string& text, TextStyle style, const std::string& fontKey) {
            Spans.emplace_back(text, Color, style, fontKey);
            return *this;
        }

        TextComponent& SpanFont(const std::string& text, const std::string& fontKey, ImU32 color, TextStyle style = TextStyle::Normal) {
            Spans.emplace_back(text, color, style, fontKey);
            return *this;
        }

        TextComponent& SpanFont(const std::string& text, const std::string& fontKey, TextStyle style = TextStyle::Normal) {
            Spans.emplace_back(text, Color, style, fontKey);
            return *this;
        }

        TextComponent& SetFont(const std::string& key) {
            FontKey = key;
            return *this;
        }

        // Set alignment (Chainable)
        TextComponent& Align(TextAlign align) {
            Alignment = align;
            return *this;
        }

        // Set flags (Chainable)
        TextComponent& SetFlags(TextFlags flags) {
            Flags = flags;
            return *this;
        }

        // Add a flag (Chainable)
        TextComponent& AddFlag(TextFlags flag) {
            Flags = Flags | flag;
            return *this;
        }

        // Set Line Height (Chainable)
        TextComponent& SetLineHeight(float height) {
            LineHeight = height;
            return *this;
        }

        // Legacy/Direct helper
        void AddSpan(const std::string& text, ImU32 color, TextStyle style = TextStyle::Normal, const std::string& fontKey = "") {
            Spans.emplace_back(text, color, style, fontKey);
        }

        // --- Per-Character Animation ---
        // Callback: index, char, position(in/out), rotation(in/out), color(in/out), scale(in/out)
        std::function<void(int index, char c, ImVec2& pos, float& rotation, ImU32& color, float& scale)> CharacterTransformCallback;
        
        // Helper accessors for backward compatibility logic inside Renderer
        bool Wrap() const { return HasFlag(Flags, TextFlags::Wrap); }
        bool Clip() const { return HasFlag(Flags, TextFlags::Clip); }
        bool PrecisionMode() const { return HasFlag(Flags, TextFlags::PrecisionMode); }
    };

    namespace TextLayout {
        using FontResolver = std::function<ImFont*(const std::string& fontKey)>;

        struct TextLayoutSpan {
            std::string Text;
            ImU32 Color = IM_COL32(255, 255, 255, 255);
            bool Bold = false;
            ImVec2 Size = ImVec2(0.0f, 0.0f);
            std::string FontKey;
            ImFont* Font = nullptr;
            float FontSize = 0.0f;
        };

        struct TextLayoutLine {
            std::vector<TextLayoutSpan> Spans;
            float Width = 0.0f;
            float Height = 0.0f;
        };

        inline std::vector<TextLayoutLine> CalculateLayout(
            const TextComponent& textComp,
            float availableWidth,
            const FontResolver& fontResolver = {}) {
            std::vector<TextLayoutLine> lines;
            if (!ImGui::GetCurrentContext()) {
                return lines;
            }

            auto measureText = [](const std::string& text, bool bold, ImFont* font) -> ImVec2 {
                (void)bold;
                if (font) {
                    const char* textBegin = text.c_str();
                    const char* textEnd = textBegin + text.size();
                    ImVec2 measured = font->CalcTextSizeA(font->FontSize, FLT_MAX, -1.0f, textBegin, textEnd, nullptr);
                    if (measured.y <= 0.0f) {
                        measured.y = font->FontSize;
                    }
                    return measured;
                }
                return ImGui::CalcTextSize(text.c_str());
            };

            std::vector<TextLayoutSpan> atoms;
            if (!textComp.Spans.empty()) {
                for (const auto& span : textComp.Spans) {
                    const bool isBold = HasStyle(span.Style, TextStyle::Bold);
                    const std::string resolvedKey = span.FontKey.empty() ? textComp.FontKey : span.FontKey;
                    ImFont* resolvedFont = fontResolver ? fontResolver(resolvedKey) : nullptr;
                    const ImVec2 measured = measureText(span.Text, isBold, resolvedFont);
                    const float fontSize = resolvedFont ? resolvedFont->FontSize : ImGui::GetFontSize();
                    atoms.push_back({ span.Text, span.Color, isBold, measured, resolvedKey, resolvedFont, fontSize });
                }
            } else {
                ImFont* resolvedFont = fontResolver ? fontResolver(textComp.FontKey) : nullptr;
                const ImVec2 measured = measureText(textComp.RawText, false, resolvedFont);
                const float fontSize = resolvedFont ? resolvedFont->FontSize : ImGui::GetFontSize();
                atoms.push_back({ textComp.RawText, textComp.Color, false, measured, textComp.FontKey, resolvedFont, fontSize });
            }

            const float lineHeight = ImGui::GetTextLineHeight() * textComp.LineHeight;
            TextLayoutLine currentLine;
            currentLine.Height = lineHeight;

            for (const auto& atom : atoms) {
                std::string remainingText = atom.Text;

                auto pushWord = [&](const std::string& word) {
                    const ImVec2 wordSize = measureText(word, atom.Bold, atom.Font);
                    if (textComp.Wrap() && currentLine.Width + wordSize.x > availableWidth && currentLine.Width > 0.0f) {
                        lines.push_back(currentLine);
                        currentLine = TextLayoutLine();
                        currentLine.Height = lineHeight;
                    }

                    currentLine.Spans.push_back({ word, atom.Color, atom.Bold, wordSize, atom.FontKey, atom.Font, atom.FontSize });
                    currentLine.Width += wordSize.x;
                    const float wordHeight = wordSize.y * textComp.LineHeight;
                    if (wordHeight > currentLine.Height) {
                        currentLine.Height = wordHeight;
                    }
                };

                auto forceNewline = [&]() {
                    lines.push_back(currentLine);
                    currentLine = TextLayoutLine();
                    currentLine.Height = lineHeight;
                };

                while (!remainingText.empty()) {
                    const size_t nextSpace = remainingText.find(' ');
                    const size_t nextNewline = remainingText.find('\n');
                    size_t splitPos = std::string::npos;
                    bool isNewline = false;

                    if (nextSpace != std::string::npos && nextNewline != std::string::npos) {
                        if (nextSpace < nextNewline) {
                            splitPos = nextSpace;
                        } else {
                            splitPos = nextNewline;
                            isNewline = true;
                        }
                    } else if (nextSpace != std::string::npos) {
                        splitPos = nextSpace;
                    } else if (nextNewline != std::string::npos) {
                        splitPos = nextNewline;
                        isNewline = true;
                    }

                    if (splitPos != std::string::npos) {
                        std::string token = remainingText.substr(0, splitPos);
                        if (isNewline) {
                            if (!token.empty()) {
                                pushWord(token);
                            }
                            forceNewline();
                            remainingText.erase(0, splitPos + 1);
                        } else {
                            token += ' ';
                            pushWord(token);
                            remainingText.erase(0, splitPos + 1);
                        }
                    } else {
                        pushWord(remainingText);
                        remainingText.clear();
                    }
                }
            }

            if (!currentLine.Spans.empty()) {
                lines.push_back(currentLine);
            }

            return lines;
        }

        inline std::vector<ImVec4> CalculateTextLines(
            const TextComponent& textComp,
            const TransformComponent& transform,
            float contentPaddingX,
            float contentPaddingY,
            float contentTopInset,
            const FontResolver& fontResolver = {}) {
            std::vector<ImVec4> rects;
            const float clampedPaddingX = (contentPaddingX < 0.0f) ? 0.0f : contentPaddingX;
            const float clampedPaddingY = (contentPaddingY < 0.0f) ? 0.0f : contentPaddingY;
            const float clampedTopInset = (contentTopInset < 0.0f) ? 0.0f : contentTopInset;
            float availableWidth = transform.Size.x - (clampedPaddingX * 2.0f);
            if (availableWidth < 1.0f) {
                availableWidth = 1.0f;
            }
            const std::vector<TextLayoutLine> lines = CalculateLayout(textComp, availableWidth, fontResolver);

            const float startX = transform.Position.x + clampedPaddingX;
            const float startY = transform.Position.y + clampedPaddingY + clampedTopInset;
            float cursorY = startY;

            for (size_t i = 0; i < lines.size(); ++i) {
                const auto& line = lines[i];
                float xOffset = 0.0f;
                float lineWidth = line.Width;

                if (textComp.Alignment == TextAlign::Center) {
                    xOffset = (availableWidth - line.Width) * 0.5f;
                } else if (textComp.Alignment == TextAlign::Right) {
                    xOffset = availableWidth - line.Width;
                } else if (textComp.Alignment == TextAlign::Justify) {
                    if (i < lines.size() - 1 && line.Spans.size() > 1 && availableWidth > line.Width) {
                        lineWidth = availableWidth;
                    }
                }

                rects.push_back(ImVec4(startX + xOffset, cursorY, lineWidth, line.Height));
                cursorY += line.Height;
            }

            return rects;
        }

        inline std::vector<ImVec4> CalculateTextLines(
            const TextComponent& textComp,
            const TransformComponent& transform,
            const ContainerComponent* container = nullptr,
            const FontResolver& fontResolver = {}) {
            const float defaultPaddingX = 10.0f;
            const float defaultPaddingY = 10.0f;
            const float defaultTopInset = (container && container->Type == ContainerType::Window) ? 30.0f : 0.0f;
            return CalculateTextLines(textComp, transform, defaultPaddingX, defaultPaddingY, defaultTopInset, fontResolver);
        }
    } // namespace TextLayout
}
