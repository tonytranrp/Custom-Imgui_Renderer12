#pragma once
#include <string>
#include <functional>

namespace RenderUtils {
    struct TextInputComponent {
        std::string Buffer;
        std::string Placeholder;
        size_t MaxLength;
        bool IsFocused;
        int CursorPos;
        std::function<void(const std::string&)> OnChange;

        TextInputComponent(const std::string& placeholder = "", size_t maxLength = 256)
            : Buffer(""), Placeholder(placeholder), MaxLength(maxLength), IsFocused(false), CursorPos(0) {}

        TextInputComponent& SetBuffer(const std::string& buffer) { Buffer = buffer; return *this; }
        TextInputComponent& SetPlaceholder(const std::string& placeholder) { Placeholder = placeholder; return *this; }
        TextInputComponent& SetMaxLength(size_t max) { MaxLength = max; return *this; }
        TextInputComponent& SetOnChange(std::function<void(const std::string&)> callback) { OnChange = callback; return *this; }
    };
}
