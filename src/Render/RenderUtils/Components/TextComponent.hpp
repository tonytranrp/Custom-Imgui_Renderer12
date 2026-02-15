#pragma once
#include "imgui.h"
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
        
        // Helper accessors for backward compatibility logic inside Renderer
        bool Wrap() const { return HasFlag(Flags, TextFlags::Wrap); }
        bool Clip() const { return HasFlag(Flags, TextFlags::Clip); }
        bool PrecisionMode() const { return HasFlag(Flags, TextFlags::PrecisionMode); }
    };
}
