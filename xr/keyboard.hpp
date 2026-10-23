#pragma once
#include <SDL.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <map>
#include "key-input.hpp"

class KeyboardTranslator {
    xkb_state* state_{};
    xkb_compose_state* compose_{};
    std::map<unsigned,std::pair<unsigned,xkb_keysym_t>> physical_modifiers_;
public:
    KeyboardTranslator(xkb_keymap* keymap, xkb_compose_table* compose, unsigned group = 0);
    ~KeyboardTranslator();
    std::vector<SDL_Event> key(unsigned code, int value);
    KeyInput translate(unsigned code, int value);
    void modifiers(unsigned depressed,unsigned latched,unsigned locked,unsigned group);
    unsigned symbol(unsigned code) const;
    void resetKeys() {
        for(auto [code,_]:physical_modifiers_)xkb_state_update_key(state_,code+8,XKB_KEY_UP);
        physical_modifiers_.clear();
    }
};
class NativeKeyboard {
    struct Device {
        int fd = -1;
        bool grabbed = false;
        std::string path;
        std::unique_ptr<KeyboardTranslator> translator;
        ~Device();
    };
    xkb_context* context_{};
    xkb_keymap* keymap_{};
    xkb_compose_table* compose_{};
    unsigned group_ = 0;
    bool desired_ = false, warned_ = false;
    std::vector<std::unique_ptr<Device>> devices_;
    std::chrono::steady_clock::time_point scanned_{};
    void scan();
public:
    NativeKeyboard();
    ~NativeKeyboard();
    void focus(bool enabled);
    std::vector<KeyInput> pump();
};
