#pragma once
#include <SDL.h>

class BackgroundShortcut {
    bool held_ = false;
public:
    bool handle(const SDL_KeyboardEvent& event, float& opacity) {
        if (event.keysym.scancode != SDL_SCANCODE_B) return false;
        // Consume release even if the modifiers were released first.
        if (event.type == SDL_KEYUP && held_) { held_ = false; return true; }
        auto mod = event.keysym.mod;
        if (!(mod & KMOD_CTRL) || !(mod & KMOD_ALT) || (mod & (KMOD_SHIFT | KMOD_GUI | KMOD_MODE))) return false;
        if (event.type == SDL_KEYDOWN) {
            if (!event.repeat && !held_) opacity = opacity > 0 ? 0.f : .3f;
            held_ = true;
        }
        return true;
    }
};
