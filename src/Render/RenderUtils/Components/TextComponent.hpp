#pragma once
#include "imgui.h"
#include "ContainerComponent.hpp"
#include "TransformComponent.hpp"
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
        
        TextSpan(const std::string& text, ImU32 color, TextStyle style = TextStyle::Normal)
            : Text(text), Color(color), Style(style) {}
    };

    // Rich text content
    struct TextComponent {
        std::string RawText; // For simple usage
        std::vector<TextSpan> Spans; // For rich text
        ImU32 Color; // Default color
        TextAlign Alignment;
        TextFlags Flags;
        float LineHeight;

        // Simple Constructor
        TextComponent(const char* text = "", ImU32 color = IM_COL32(255,255,255,255), TextFlags flags = TextFlags::None, TextAlign align = TextAlign::Left)
            : RawText(text), Color(color), Alignment(align), Flags(flags), LineHeight(1.0f) {}

        // --- Fluent Builder API ---

        // Add a rich text span (Chainable)
        TextComponent& Span(const std::string& text, ImU32 color, TextStyle style = TextStyle::Normal) {
            Spans.emplace_back(text, color, style);
            return *this;
        }

        // Add a span with default color (Chainable)
        TextComponent& Span(const std::string& text, TextStyle style = TextStyle::Normal) {
            Spans.emplace_back(text, Color, style);
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
        void AddSpan(const std::string& text, ImU32 color, TextStyle style = TextStyle::Normal) {
            Spans.emplace_back(text, color, style);
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
        struct TextLayoutSpan {
            std::string Text;
            ImU32 Color = IM_COL32(255, 255, 255, 255);
            bool Bold = false;
            ImVec2 Size = ImVec2(0.0f, 0.0f);
        };

        struct TextLayoutLine {
            std::vector<TextLayoutSpan> Spans;
            float Width = 0.0f;
            float Height = 0.0f;
        };

        inline std::vector<TextLayoutLine> CalculateLayout(const TextComponent& textComp, float availableWidth) {
            std::vector<TextLayoutLine> lines;
            if (!ImGui::GetCurrentContext()) {
                return lines;
            }

            auto measureText = [](const std::string& text, bool bold) -> ImVec2 {
                (void)bold;
                return ImGui::CalcTextSize(text.c_str());
            };

            std::vector<TextLayoutSpan> atoms;
            if (!textComp.Spans.empty()) {
                for (const auto& span : textComp.Spans) {
                    const bool isBold = HasStyle(span.Style, TextStyle::Bold);
                    atoms.push_back({ span.Text, span.Color, isBold, measureText(span.Text, isBold) });
                }
            } else {
                atoms.push_back({ textComp.RawText, textComp.Color, false, measureText(textComp.RawText, false) });
            }

            const float lineHeight = ImGui::GetTextLineHeight() * textComp.LineHeight;
            TextLayoutLine currentLine;
            currentLine.Height = lineHeight;

            for (const auto& atom : atoms) {
                std::string remainingText = atom.Text;

                auto pushWord = [&](const std::string& word) {
                    const ImVec2 wordSize = measureText(word, atom.Bold);
                    if (textComp.Wrap() && currentLine.Width + wordSize.x > availableWidth && currentLine.Width > 0.0f) {
                        lines.push_back(currentLine);
                        currentLine = TextLayoutLine();
                        currentLine.Height = lineHeight;
                    }

                    currentLine.Spans.push_back({ word, atom.Color, atom.Bold, wordSize });
                    currentLine.Width += wordSize.x;
                    if (wordSize.y > currentLine.Height) {
                        currentLine.Height = wordSize.y;
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
            const ContainerComponent* container = nullptr) {
            std::vector<ImVec4> rects;
            const float availableWidth = transform.Size.x - 20.0f;
            const std::vector<TextLayoutLine> lines = CalculateLayout(textComp, availableWidth);

            const float startX = transform.Position.x + 10.0f;
            const float startY = transform.Position.y + ((container && container->Type == ContainerType::Window) ? 40.0f : 10.0f);
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
    } // namespace TextLayout
}
