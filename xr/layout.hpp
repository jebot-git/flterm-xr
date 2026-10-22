#pragma once
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>

struct PanelLayout {
    float width, height, x, y, z;
    bool operator==(const PanelLayout&) const = default;
    void move(float dx, float dy, float dz) {
        x = std::clamp(x+dx, -10.f, 10.f);
        y = std::clamp(y+dy, -10.f, 10.f);
        z = std::clamp(z+dz, -20.f, -.2f);
    }
    void resize(float dw, float dh) {
        width = std::clamp(width+dw, .05f, 10.f);
        height = std::clamp(height+dh, .05f, 10.f);
    }
    void scale(float factor) {
        float low = std::max(.05f/width, .05f/height);
        float high = std::min(10.f/width, 10.f/height);
        if (low > high) return;
        factor = std::clamp(factor, low, high);
        width *= factor; height *= factor;
    }
};

class LayoutShortcuts {
    std::array<bool, SDL_NUM_SCANCODES> held_{};
public:
    bool handle(const SDL_KeyboardEvent& event, PanelLayout& layout, const PanelLayout& initial) {
        auto sc = event.keysym.scancode;
        if (sc < 0 || sc >= SDL_NUM_SCANCODES) return false;
        // Suppress release even if Ctrl/Alt was released before the action key.
        if (event.type == SDL_KEYUP && held_[sc]) { held_[sc] = false; return true; }
        auto mod = event.keysym.mod;
        if (!(mod & KMOD_CTRL) || !(mod & KMOD_ALT) || (mod & (KMOD_GUI | KMOD_MODE))) return false;
        bool recognized = sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT ||
            sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_PAGEUP ||
            sc == SDL_SCANCODE_PAGEDOWN || sc == SDL_SCANCODE_MINUS || sc == SDL_SCANCODE_EQUALS ||
            sc == SDL_SCANCODE_KP_MINUS || sc == SDL_SCANCODE_KP_PLUS || sc == SDL_SCANCODE_HOME;
        if (!recognized) return false;
        if (event.type != SDL_KEYDOWN) return true;
        held_[sc] = true;
        constexpr float step = .05f;
        bool resize = mod & KMOD_SHIFT;
        switch (sc) {
            case SDL_SCANCODE_LEFT: if (resize) layout.resize(-step, 0); else layout.move(-step, 0, 0); break;
            case SDL_SCANCODE_RIGHT: if (resize) layout.resize(step, 0); else layout.move(step, 0, 0); break;
            case SDL_SCANCODE_UP: if (resize) layout.resize(0, step); else layout.move(0, step, 0); break;
            case SDL_SCANCODE_DOWN: if (resize) layout.resize(0, -step); else layout.move(0, -step, 0); break;
            case SDL_SCANCODE_PAGEUP: layout.move(0, 0, step); break;
            case SDL_SCANCODE_PAGEDOWN: layout.move(0, 0, -step); break;
            case SDL_SCANCODE_EQUALS: case SDL_SCANCODE_KP_PLUS: layout.scale(1.05f); break;
            case SDL_SCANCODE_MINUS: case SDL_SCANCODE_KP_MINUS: layout.scale(1/1.05f); break;
            case SDL_SCANCODE_HOME: layout = initial; break;
            default: break;
        }
        return true;
    }
};
