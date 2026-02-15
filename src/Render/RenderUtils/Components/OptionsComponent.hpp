#pragma once
#include <vector>
#include <string>
#include <functional>

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
}
