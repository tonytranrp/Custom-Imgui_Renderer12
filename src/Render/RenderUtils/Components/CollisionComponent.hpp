#pragma once

namespace RenderUtils {
    // Collision Component
    // If present, this entity will collide with others that have CollisionComponent (and same parent)
    struct CollisionComponent {
        bool Collides;

        CollisionComponent(bool collides = true) : Collides(collides) {}

        CollisionComponent& SetCollides(bool collides) { Collides = collides; return *this; }
    };
}
