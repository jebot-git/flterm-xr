#include "layout.hpp"
#include "background.hpp"
#include <iostream>
#include <stdexcept>
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static bool near(float a, float b) { return std::abs(a-b) < 1e-5f; }
int main() try {
    const PanelLayout initial{1.2f, .8f, .25f, -.15f, -1.4f};
    PanelLayout layout = initial;
    LayoutShortcuts shortcuts;
    BackgroundShortcut background;
    float opacity = 0;
    SDL_KeyboardEvent toggle{}; toggle.type = SDL_KEYDOWN; toggle.keysym.scancode = SDL_SCANCODE_B;
    require(!background.handle(toggle, opacity) && opacity == 0, "Plain B must pass to the shell");
    toggle.keysym.mod = KMOD_CTRL;
    require(!background.handle(toggle, opacity), "Ctrl+B must pass to the shell");
    toggle.keysym.mod = KMOD_CTRL | KMOD_ALT;
    require(background.handle(toggle, opacity) && near(opacity, .3f), "Background must toggle to 70% transparency");
    toggle.repeat = 1;
    require(background.handle(toggle, opacity) && near(opacity, .3f), "Held background shortcut must not toggle repeatedly");
    toggle.type = SDL_KEYUP; toggle.keysym.mod = 0;
    require(background.handle(toggle, opacity) && near(opacity, .3f), "Background release without modifiers must be consumed");
    toggle.type = SDL_KEYDOWN; toggle.repeat = 0; toggle.keysym.mod = KMOD_CTRL | KMOD_ALT;
    require(background.handle(toggle, opacity) && opacity == 0, "Second press must restore full transparency");
    toggle.type = SDL_KEYUP; background.handle(toggle, opacity);
    opacity = .7f;
    toggle.type = SDL_KEYDOWN;
    require(background.handle(toggle, opacity) && opacity == 0, "Custom opacity must toggle to full transparency");
    toggle.type = SDL_KEYUP; background.handle(toggle, opacity);
    toggle.type = SDL_KEYDOWN; toggle.keysym.mod |= KMOD_SHIFT;
    require(!background.handle(toggle, opacity), "Other modifier combinations must pass to the shell");
    auto press = [&](SDL_Scancode key, bool shift = false, bool repeat = false) {
        SDL_KeyboardEvent event{}; event.type = SDL_KEYDOWN; event.keysym.scancode = key;
        event.keysym.mod = KMOD_CTRL | KMOD_ALT | (shift ? KMOD_SHIFT : 0); event.repeat = repeat;
        return shortcuts.handle(event, layout, initial);
    };
    require(press(SDL_SCANCODE_RIGHT) && near(layout.x, .3f), "Right must move 5cm");
    press(SDL_SCANCODE_LEFT); press(SDL_SCANCODE_UP); press(SDL_SCANCODE_DOWN);
    require(near(layout.x, initial.x) && near(layout.y, initial.y), "Arrow movement must cancel");
    press(SDL_SCANCODE_PAGEUP); require(near(layout.z, -1.35f), "PageUp must move nearer");
    press(SDL_SCANCODE_PAGEDOWN); require(near(layout.z, initial.z), "PageDown must move farther");
    press(SDL_SCANCODE_RIGHT, true); press(SDL_SCANCODE_UP, true);
    require(near(layout.width, 1.25f) && near(layout.height, .85f) && near(layout.x, initial.x), "Shift+arrows must resize, not move");
    float ratio = layout.width/layout.height;
    press(SDL_SCANCODE_EQUALS);
    require(near(layout.width, 1.3125f) && near(layout.width/layout.height, ratio), "Scale must preserve aspect");
    press(SDL_SCANCODE_MINUS); require(near(layout.width, 1.25f), "Scale up/down must cancel");
    press(SDL_SCANCODE_HOME);
    require(near(layout.width, initial.width) && near(layout.z, initial.z) && near(layout.x, initial.x), "Reset must restore CLI launch layout");
    press(SDL_SCANCODE_RIGHT, false, true); require(near(layout.x, .3f), "Held keys must repeat movement");
    SDL_KeyboardEvent release{}; release.type = SDL_KEYUP; release.keysym.scancode = SDL_SCANCODE_RIGHT;
    require(shortcuts.handle(release, layout, initial) && near(layout.x, .3f), "Release without modifiers must be consumed without moving");
    SDL_KeyboardEvent plain{}; plain.type = SDL_KEYDOWN; plain.keysym.scancode = SDL_SCANCODE_LEFT;
    require(!shortcuts.handle(plain, layout, initial), "Plain arrows must pass to the shell");
    plain.keysym.mod = KMOD_CTRL; plain.keysym.scancode = SDL_SCANCODE_PAGEDOWN;
    require(!shortcuts.handle(plain, layout, initial), "Ctrl+PageDown must remain available for tabs");
    plain.keysym.mod = KMOD_CTRL | KMOD_ALT; plain.keysym.scancode = SDL_SCANCODE_1;
    require(!shortcuts.handle(plain, layout, initial), "Ctrl+Alt+digits must remain available for tabs");
    layout.move(100, -100, 100); require(layout.x == 10 && layout.y == -10 && layout.z == -.2f, "Position limits failed");
    layout.move(0, 0, -100); require(layout.z == -20, "Far plane limit failed");
    layout.resize(-100, 100); require(layout.width == .05f && layout.height == 10, "Size limits failed");
    layout = initial;
    for (int i = 0; i < 200; ++i) layout.scale(1.05f);
    require(layout.width <= 10 && layout.height <= 10 && near(layout.width/layout.height, initial.width/initial.height), "Scale bounds/aspect failed");
    std::cout << "Layout shortcuts, repeats, release routing, tab/shell keys, reset and bounds passed\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
