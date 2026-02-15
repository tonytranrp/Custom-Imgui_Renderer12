#pragma once

namespace RenderUtils {

    struct TransparencyComponent {
        float Alpha; // 0.0f (Invisible) to 1.0f (Opaque)

        TransparencyComponent(float alpha = 1.0f) : Alpha(alpha) {}

        TransparencyComponent& SetAlpha(float alpha) { Alpha = alpha; return *this; }
    };
}
