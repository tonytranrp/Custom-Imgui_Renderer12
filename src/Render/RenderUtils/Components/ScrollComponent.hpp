#pragma once

namespace RenderUtils {
    struct ScrollComponent {
        float ScrollY;        // Current scroll offset
        float ContentHeight;  // Total height of content (set by user/layout)
        float ViewHeight;     // Visible height (set by user/layout)
        bool  ShowScrollbar;
        float Speed;          // Scroll speed

        ScrollComponent(float scrollY = 0.0f, float contentHeight = 0.0f, float viewHeight = 0.0f, bool showBar = true, float speed = 20.0f) 
            : ScrollY(scrollY), ContentHeight(contentHeight), ViewHeight(viewHeight), ShowScrollbar(showBar), Speed(speed) {}

        ScrollComponent& SetScrollY(float y) { ScrollY = y; return *this; }
        ScrollComponent& SetContentHeight(float h) { ContentHeight = h; return *this; }
        ScrollComponent& SetViewHeight(float h) { ViewHeight = h; return *this; }
        ScrollComponent& SetShowScrollbar(bool show) { ShowScrollbar = show; return *this; }
        ScrollComponent& SetSpeed(float speed) { Speed = speed; return *this; }
    };
}
