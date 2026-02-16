#pragma once
#include <algorithm>
#include <vector>
#include <string>
#include <functional>

#include "entt/entt.hpp"
#include "imgui.h"

#include "InputStateComponent.hpp"
#include "StyleComponent.hpp"
#include "TransformComponent.hpp"
#include "WindowHeaderComponent.hpp"

namespace RenderUtils {
    struct OptionsComponent {
        std::vector<std::string> Options;
        int SelectedIndex;
        std::function<void(int)> OnSelectCallback;

        OptionsComponent(std::vector<std::string> options = {}, int selected = 0, std::function<void(int)> callback = nullptr)
            : Options(options), SelectedIndex(selected), OnSelectCallback(callback) {}

        OptionsComponent& SetOptions(const std::vector<std::string>& options) { Options = options; return *this; }
        OptionsComponent& SetSelectedIndex(int index) { SelectedIndex = index; return *this; }
        OptionsComponent& SetCallback(std::function<void(int)> callback) { OnSelectCallback = callback; return *this; }
    };

    namespace OptionsSystem {
        inline void Update(entt::registry& registry) {
            const ImVec2 mousePos = ImGui::GetMousePos();
            auto view = registry.view<OptionsComponent, InputStateComponent, TransformComponent, StyleComponent>();
            for (auto entity : view) {
                auto& options = view.get<OptionsComponent>(entity);
                auto& input = view.get<InputStateComponent>(entity);
                const auto& transform = view.get<TransformComponent>(entity);
                const auto& style = view.get<StyleComponent>(entity);

                if (!style.CalculatedVisible || !input.JustPressed || !input.IsHovered) {
                    continue;
                }
                const int optionCount = static_cast<int>(options.Options.size());
                if (optionCount <= 0) {
                    continue;
                }

                const float topInset = WindowHeaderSystem::GetContentTopInset(registry, entity);
                ImVec2 optionMin = ImVec2(
                    transform.Position.x + style.ContentPaddingX,
                    transform.Position.y + style.ContentPaddingY + topInset);
                ImVec2 optionMax = ImVec2(
                    transform.Position.x + transform.Size.x - style.ContentPaddingX,
                    transform.Position.y + transform.Size.y - style.ContentPaddingY);
                if (optionMax.x <= optionMin.x + 4.0f || optionMax.y <= optionMin.y + 4.0f) {
                    optionMin = transform.Position;
                    optionMax = ImVec2(transform.Position.x + transform.Size.x, transform.Position.y + transform.Size.y);
                }

                if (mousePos.x < optionMin.x || mousePos.x > optionMax.x ||
                    mousePos.y < optionMin.y || mousePos.y > optionMax.y) {
                    continue;
                }

                const float width = optionMax.x - optionMin.x;
                if (width <= 0.0f) {
                    continue;
                }

                int selected = static_cast<int>(((mousePos.x - optionMin.x) / width) * static_cast<float>(optionCount));
                if (selected < 0) {
                    selected = 0;
                }
                if (selected >= optionCount) {
                    selected = optionCount - 1;
                }

                if (selected != options.SelectedIndex) {
                    options.SelectedIndex = selected;
                    if (options.OnSelectCallback) {
                        options.OnSelectCallback(options.SelectedIndex);
                    }
                }
            }
        }
    } // namespace OptionsSystem
}
