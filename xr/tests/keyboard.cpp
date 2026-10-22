#include "keyboard.hpp"
#include "layout.hpp"
#include <linux/input-event-codes.h>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() try {
    auto context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names names{}; names.layout = "us"; names.variant = "intl";
    auto keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    auto compose = xkb_compose_table_new_from_locale(context, "C.UTF-8", XKB_COMPOSE_COMPILE_NO_FLAGS);
    require(context && keymap && compose, "Cannot initialize test keyboard");
    {
        KeyboardTranslator keyboard(keymap, compose);
        auto input = keyboard.key(KEY_A, 1);
        require(input.size() == 2 && input[0].key.keysym.scancode == SDL_SCANCODE_A &&
                std::string(input[1].text.text) == "a", "Plain keys must produce text");
        input = keyboard.key(KEY_A, 2);
        require(input.size() == 2 && input[0].key.repeat && std::string(input[1].text.text) == "a", "Repeats must produce text");
        input = keyboard.key(KEY_A, 0);
        require(input.size() == 1 && input[0].type == SDL_KEYUP, "Releases must not produce text");
        keyboard.key(KEY_LEFTSHIFT, 1);
        input = keyboard.key(KEY_B, 1);
        require(input.size() == 2 && std::string(input[1].text.text) == "B", "Shift must capitalize text");
        keyboard.key(KEY_B, 0); keyboard.key(KEY_LEFTSHIFT, 0);
        keyboard.key(KEY_LEFTCTRL, 1);
        input = keyboard.key(KEY_C, 1);
        require(input.size() == 1 && (input[0].key.keysym.mod & KMOD_CTRL), "Ctrl+C must go through terminal key encoding");
        keyboard.key(KEY_C, 0);
        for (auto code : {KEY_A, KEY_E, KEY_U, KEY_W, KEY_K, KEY_D, KEY_Z, KEY_S, KEY_Q}) {
            input = keyboard.key(code, 1);
            require(input.size() == 1 && input[0].type == SDL_KEYDOWN &&
                    (input[0].key.keysym.mod & KMOD_CTRL) && input[0].key.keysym.scancode != SDL_SCANCODE_UNKNOWN,
                    "TTY Ctrl chords must produce one terminal key event and no duplicate text");
            keyboard.key(code, 0);
        }
        keyboard.key(KEY_LEFTALT, 1);
        input = keyboard.key(KEY_RIGHT, 1);
        PanelLayout initial{1.2f, .8f, 0, -.15f, -1.2f}, layout = initial;
        LayoutShortcuts shortcuts;
        require(input.size() == 1 && shortcuts.handle(input[0].key, layout, initial) && layout.x > initial.x,
                "Native Ctrl+Alt+Right must reach layout shortcuts");
        keyboard.key(KEY_RIGHT, 0); keyboard.key(KEY_LEFTALT, 0); keyboard.key(KEY_LEFTCTRL, 0);
        input = keyboard.key(KEY_APOSTROPHE, 1);
        require(input.size() == 1, "Dead key must wait for composition");
        keyboard.key(KEY_APOSTROPHE, 0);
        input = keyboard.key(KEY_E, 1);
        require(input.size() == 2 && std::string(input[1].text.text) == "é", "Compose must produce UTF-8");
        keyboard.key(KEY_E, 0);
        for (auto code : {KEY_ENTER, KEY_TAB, KEY_BACKSPACE, KEY_F12}) {
            input = keyboard.key(code, 1);
            require(input.size() == 1 && input[0].key.keysym.scancode != SDL_SCANCODE_UNKNOWN,
                    "Control/navigation keys must not produce duplicate text");
            keyboard.key(code, 0);
        }
        keyboard.key(KEY_LEFTMETA, 1);
        input = keyboard.key(KEY_F12, 1);
        require(input.size() == 1 && (input[0].key.keysym.mod & KMOD_GUI), "Meta+F12 must remain recognizable while grabbed");
    }
    {
        names.layout="us,cz";names.variant=nullptr;
        auto groups=xkb_keymap_new_from_names(context,&names,XKB_KEYMAP_COMPILE_NO_FLAGS);
        require(groups,"Cannot initialize DomeD layout groups");
        KeyboardTranslator czech(groups,compose,1);
        auto input=czech.key(KEY_Y,1);
        require(input.size()==2 && std::string(input[1].text.text)=="z","Native overlay must use DomeD's active Czech group");
        xkb_keymap_unref(groups);
    }
    xkb_compose_table_unref(compose); xkb_keymap_unref(keymap); xkb_context_unref(context);
    std::cout << "Native keyboard text, modifiers, repeats, shortcuts and UTF-8 composition passed\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
