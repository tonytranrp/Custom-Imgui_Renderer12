#pragma once

#include <string>

namespace RenderUtils {
    // Type of the container
    enum class ContainerType {
        Window,
        Panel,
        Button,
        Label
    };

    struct ContainerComponent {
        ContainerType Type;
        const char* Name;
        std::string OwnedName;

        ContainerComponent(ContainerType type = ContainerType::Panel, const char* name = "Container")
            : Type(type) {
            SetName(name);
        }

        ContainerComponent(ContainerType type, const std::string& name)
            : Type(type) {
            SetName(name);
        }

        ContainerComponent(const ContainerComponent& other)
            : Type(other.Type),
              OwnedName(other.OwnedName) {
            Name = OwnedName.c_str();
        }

        ContainerComponent(ContainerComponent&& other) noexcept
            : Type(other.Type),
              OwnedName(std::move(other.OwnedName)) {
            Name = OwnedName.c_str();
        }

        ContainerComponent& operator=(const ContainerComponent& other) {
            if (this == &other) {
                return *this;
            }
            Type = other.Type;
            OwnedName = other.OwnedName;
            Name = OwnedName.c_str();
            return *this;
        }

        ContainerComponent& operator=(ContainerComponent&& other) noexcept {
            if (this == &other) {
                return *this;
            }
            Type = other.Type;
            OwnedName = std::move(other.OwnedName);
            Name = OwnedName.c_str();
            return *this;
        }

        ContainerComponent& SetType(ContainerType type) { Type = type; return *this; }
        ContainerComponent& SetName(const char* name) {
            OwnedName = name ? name : "";
            Name = OwnedName.c_str();
            return *this;
        }
        ContainerComponent& SetName(const std::string& name) {
            OwnedName = name;
            Name = OwnedName.c_str();
            return *this;
        }
    };
}
