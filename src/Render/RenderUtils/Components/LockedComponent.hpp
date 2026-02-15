#pragma once

namespace RenderUtils {
    // Explicitly lock an entity from being interacted with (even in debug mode if desired, though debug usually overrides)
    // Actually, debug mode should override unless explicitly stated "SYSTEM_LOCKED" or similar.
    // For now, let's just say Locked = No interaction ever.
    struct LockedComponent {
        bool Locked;

        LockedComponent(bool locked = true) : Locked(locked) {}

        LockedComponent& SetLocked(bool locked) { Locked = locked; return *this; }
    };
}
