#pragma once
#include <algorithm>
#include <string>
#include <functional>

#include "entt/entt.hpp"
#include "imgui.h"

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

        TextInputComponent& SetBuffer(const std::string& buffer) { 
            Buffer = buffer; 
            CursorPos = (int)Buffer.length(); // Auto-move cursor to end
            return *this; 
        }
        TextInputComponent& SetPlaceholder(const std::string& placeholder) { Placeholder = placeholder; return *this; }
        TextInputComponent& SetMaxLength(size_t max) { MaxLength = max; return *this; }
        TextInputComponent& SetOnChange(std::function<void(const std::string&)> callback) { OnChange = callback; return *this; }
    };

    namespace TextInputSystem {
        namespace detail {
            inline void ClampCursor(TextInputComponent& input) {
                if (input.CursorPos < 0) {
                    input.CursorPos = 0;
                }
                const int maxCursor = static_cast<int>(input.Buffer.length());
                if (input.CursorPos > maxCursor) {
                    input.CursorPos = maxCursor;
                }
            }

            inline bool EraseBeforeCursor(TextInputComponent& input) {
                ClampCursor(input);
                if (input.CursorPos <= 0 || input.Buffer.empty()) {
                    return false;
                }

                input.Buffer.erase(static_cast<size_t>(input.CursorPos - 1), 1);
                --input.CursorPos;
                return true;
            }

            inline bool EraseAtCursor(TextInputComponent& input) {
                ClampCursor(input);
                if (input.CursorPos < 0 || input.CursorPos >= static_cast<int>(input.Buffer.length())) {
                    return false;
                }

                input.Buffer.erase(static_cast<size_t>(input.CursorPos), 1);
                return true;
            }

            inline bool InsertText(TextInputComponent& input, const std::string& value) {
                if (value.empty()) {
                    return false;
                }

                ClampCursor(input);
                if (input.Buffer.length() >= input.MaxLength) {
                    return false;
                }

                const size_t available = input.MaxLength - input.Buffer.length();
                const size_t copyCount = (std::min)(available, value.length());
                if (copyCount == 0) {
                    return false;
                }

                input.Buffer.insert(static_cast<size_t>(input.CursorPos), value.c_str(), copyCount);
                input.CursorPos += static_cast<int>(copyCount);
                return true;
            }
        }

        inline void Update(entt::registry& registry) {
            auto view = registry.view<TextInputComponent>();
            for (auto entity : view) {
                auto& textInput = view.get<TextInputComponent>(entity);
                if (!textInput.IsFocused) {
                    continue;
                }

                ImGuiIO& io = ImGui::GetIO();
                detail::ClampCursor(textInput);

                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && textInput.CursorPos > 0) {
                    --textInput.CursorPos;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && textInput.CursorPos < static_cast<int>(textInput.Buffer.length())) {
                    ++textInput.CursorPos;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
                    textInput.CursorPos = 0;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_End)) {
                    textInput.CursorPos = static_cast<int>(textInput.Buffer.length());
                }

                bool changed = false;
                if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
                    changed = detail::EraseBeforeCursor(textInput) || changed;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Delete, true)) {
                    changed = detail::EraseAtCursor(textInput) || changed;
                }

                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
                    const char* clipboard = ImGui::GetClipboardText();
                    if (clipboard) {
                        changed = detail::InsertText(textInput, clipboard) || changed;
                    }
                }

                for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
                    const ImWchar wcharInput = io.InputQueueCharacters[i];
                    if (wcharInput <= 0 || wcharInput >= 0x80 || wcharInput < 32) {
                        continue;
                    }

                    const char c = static_cast<char>(wcharInput);
                    changed = detail::InsertText(textInput, std::string(1, c)) || changed;
                }

                detail::ClampCursor(textInput);
                if (changed && textInput.OnChange) {
                    textInput.OnChange(textInput.Buffer);
                }
            }
        }
    } // namespace TextInputSystem
}
