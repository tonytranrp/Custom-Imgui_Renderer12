#pragma once

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

        ContainerComponent(ContainerType type = ContainerType::Panel, const char* name = "Container")
            : Type(type), Name(name) {}

        ContainerComponent& SetType(ContainerType type) { Type = type; return *this; }
        ContainerComponent& SetName(const char* name) { Name = name; return *this; }
    };
}
