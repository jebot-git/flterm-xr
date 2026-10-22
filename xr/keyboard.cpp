#include "keyboard.hpp"
#include <nlohmann/json.hpp>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <cstring>

static SDL_Scancode scancode(unsigned code) {
    switch (code) {
#define KEYMAP(a,b) case KEY_##a: return SDL_SCANCODE_##b
        KEYMAP(A,A); KEYMAP(B,B); KEYMAP(C,C); KEYMAP(D,D); KEYMAP(E,E); KEYMAP(F,F); KEYMAP(G,G);
        KEYMAP(H,H); KEYMAP(I,I); KEYMAP(J,J); KEYMAP(K,K); KEYMAP(L,L); KEYMAP(M,M); KEYMAP(N,N);
        KEYMAP(O,O); KEYMAP(P,P); KEYMAP(Q,Q); KEYMAP(R,R); KEYMAP(S,S); KEYMAP(T,T); KEYMAP(U,U);
        KEYMAP(V,V); KEYMAP(W,W); KEYMAP(X,X); KEYMAP(Y,Y); KEYMAP(Z,Z);
        KEYMAP(0,0); KEYMAP(1,1); KEYMAP(2,2); KEYMAP(3,3); KEYMAP(4,4);
        KEYMAP(5,5); KEYMAP(6,6); KEYMAP(7,7); KEYMAP(8,8); KEYMAP(9,9);
        KEYMAP(F1,F1); KEYMAP(F2,F2); KEYMAP(F3,F3); KEYMAP(F4,F4); KEYMAP(F5,F5); KEYMAP(F6,F6);
        KEYMAP(F7,F7); KEYMAP(F8,F8); KEYMAP(F9,F9); KEYMAP(F10,F10); KEYMAP(F11,F11); KEYMAP(F12,F12);
        KEYMAP(ENTER,RETURN); KEYMAP(ESC,ESCAPE); KEYMAP(TAB,TAB); KEYMAP(BACKSPACE,BACKSPACE);
        KEYMAP(LEFT,LEFT); KEYMAP(RIGHT,RIGHT); KEYMAP(UP,UP); KEYMAP(DOWN,DOWN);
        KEYMAP(PAGEUP,PAGEUP); KEYMAP(PAGEDOWN,PAGEDOWN); KEYMAP(HOME,HOME); KEYMAP(END,END);
        KEYMAP(INSERT,INSERT); KEYMAP(DELETE,DELETE); KEYMAP(SPACE,SPACE);
        KEYMAP(LEFTCTRL,LCTRL); KEYMAP(RIGHTCTRL,RCTRL); KEYMAP(LEFTSHIFT,LSHIFT); KEYMAP(RIGHTSHIFT,RSHIFT);
        KEYMAP(LEFTALT,LALT); KEYMAP(RIGHTALT,RALT); KEYMAP(LEFTMETA,LGUI); KEYMAP(RIGHTMETA,RGUI);
        KEYMAP(MINUS,MINUS); KEYMAP(EQUAL,EQUALS); KEYMAP(LEFTBRACE,LEFTBRACKET); KEYMAP(RIGHTBRACE,RIGHTBRACKET);
        KEYMAP(BACKSLASH,BACKSLASH); KEYMAP(SEMICOLON,SEMICOLON); KEYMAP(APOSTROPHE,APOSTROPHE);
        KEYMAP(GRAVE,GRAVE); KEYMAP(COMMA,COMMA); KEYMAP(DOT,PERIOD); KEYMAP(SLASH,SLASH);
        KEYMAP(KPPLUS,KP_PLUS); KEYMAP(KPMINUS,KP_MINUS); KEYMAP(KPENTER,RETURN);
#undef KEYMAP
        default: return SDL_SCANCODE_UNKNOWN;
    }
}
KeyboardTranslator::KeyboardTranslator(xkb_keymap* keymap, xkb_compose_table* compose, unsigned group) {
    state_ = xkb_state_new(keymap);
    if (!state_) throw std::runtime_error("Cannot create native keyboard state");
    xkb_state_update_mask(state_,0,0,0,0,0,group);
    if (compose) compose_ = xkb_compose_state_new(compose, XKB_COMPOSE_STATE_NO_FLAGS);
}
KeyboardTranslator::~KeyboardTranslator() {
    if (compose_) xkb_compose_state_unref(compose_);
    xkb_state_unref(state_);
}
std::vector<SDL_Event> KeyboardTranslator::key(unsigned code, int value) {
    std::vector<SDL_Event> events;
    auto keycode = code+8;
    if (value != 2) xkb_state_update_key(state_, keycode, value ? XKB_KEY_DOWN : XKB_KEY_UP);
    auto mod = [&](const char* name) { return xkb_state_mod_name_is_active(state_, name, XKB_STATE_MODS_EFFECTIVE) > 0; };
    SDL_Keymod mods = SDL_Keymod((mod(XKB_MOD_NAME_SHIFT) ? KMOD_SHIFT : 0) |
        (mod(XKB_MOD_NAME_CTRL) ? KMOD_CTRL : 0) | (mod(XKB_MOD_NAME_ALT) ? KMOD_ALT : 0) |
        (mod(XKB_MOD_NAME_LOGO) ? KMOD_GUI : 0));
    SDL_Event event{};
    event.type = value ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.repeat = value == 2;
    event.key.keysym.scancode = scancode(code);
    event.key.keysym.mod = mods;
    events.push_back(event);
    if (!value || (mods & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) return events;
    char text[32]{};
    auto sym = xkb_state_key_get_one_sym(state_, keycode);
    if (compose_) {
        xkb_compose_state_feed(compose_, sym);
        auto status = xkb_compose_state_get_status(compose_);
        if (status == XKB_COMPOSE_COMPOSING) return events;
        if (status == XKB_COMPOSE_COMPOSED) {
            xkb_compose_state_get_utf8(compose_, text, sizeof(text));
            xkb_compose_state_reset(compose_);
        } else if (status == XKB_COMPOSE_CANCELLED) { xkb_compose_state_reset(compose_); return events; }
        else xkb_state_key_get_utf8(state_, keycode, text, sizeof(text));
    } else xkb_state_key_get_utf8(state_, keycode, text, sizeof(text));
    // Navigation/control keys are encoded by Ghostty through the key event.
    if (static_cast<unsigned char>(text[0]) >= 32 && text[0] != 127) {
        SDL_Event input{}; input.type = SDL_TEXTINPUT;
        memcpy(input.text.text, text, sizeof(text));
        events.push_back(input);
    }
    return events;
}
NativeKeyboard::Device::~Device() {
    if (fd >= 0) { if (grabbed) ioctl(fd, EVIOCGRAB, 0); close(fd); }
}
NativeKeyboard::NativeKeyboard() {
    context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context_) throw std::runtime_error("Cannot initialize xkbcommon");
    std::string layout, variant, options;
    auto config = getenv("XDG_CONFIG_HOME");
    auto home = getenv("HOME");
    std::filesystem::path directory = config && *config ? config : std::string(home ? home : "")+"/.config";
    std::ifstream kde(directory/"kxkbrc");
    std::string line;
    while (std::getline(kde, line)) {
        if (line.starts_with("LayoutList=")) layout = line.substr(11);
        if (line.starts_with("VariantList=")) variant = line.substr(12);
    }
    const char *desktop=getenv("XDG_CURRENT_DESKTOP");
    if (desktop && std::string(desktop).find("Dome")!=std::string::npos) {
        try {
            std::ifstream input(directory/"dome/keyboard.json");
            if (input) {
                auto settings=nlohmann::json::parse(input);
                layout=settings.value("layout",std::string("us"));
                variant=settings.value("variant",std::string());
                options=settings.value("options",std::string());
                group_=settings.value("active",0u);
            }
        } catch (const std::exception &error) { std::cerr<<"Native keyboard: "<<error.what()<<'\n'; }
    }
    xkb_rule_names names{};
    names.layout = getenv("XKB_DEFAULT_LAYOUT");
    names.variant = getenv("XKB_DEFAULT_VARIANT");
    names.options = getenv("XKB_DEFAULT_OPTIONS");
    if (!names.options && !options.empty()) names.options=options.c_str();
    if (!names.layout && !layout.empty()) names.layout = layout.c_str();
    if (!names.variant && !variant.empty()) names.variant = variant.c_str();
    keymap_ = xkb_keymap_new_from_names(context_, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap_) throw std::runtime_error("Cannot load the native keyboard layout");
    const char* locale = getenv("LC_ALL");
    if (!locale || !*locale) locale = getenv("LC_CTYPE");
    if (!locale || !*locale) locale = getenv("LANG");
    if (!locale || !*locale) locale = "C.UTF-8";
    compose_ = xkb_compose_table_new_from_locale(context_, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
}
NativeKeyboard::~NativeKeyboard() {
    devices_.clear();
    if (compose_) xkb_compose_table_unref(compose_);
    if (keymap_) xkb_keymap_unref(keymap_);
    if (context_) xkb_context_unref(context_);
}
void NativeKeyboard::focus(bool enabled) {
    if (desired_ == enabled) return;
    desired_ = enabled;
    if (!enabled) { devices_.clear(); std::cerr << "Native keyboard released\n"; }
    else { scanned_ = {}; std::cerr << "Native keyboard focus requested\n"; }
}
static bool bit(const std::array<unsigned char, (KEY_CNT+7)/8>& bits, unsigned key) {
    return bits[key/8] & (1u << (key%8));
}
void NativeKeyboard::scan() {
    scanned_ = std::chrono::steady_clock::now();
    std::error_code error;
    for (auto& item : std::filesystem::directory_iterator("/dev/input", error)) {
        auto path = item.path().string();
        if (!item.path().filename().string().starts_with("event")) continue;
        if (std::any_of(devices_.begin(), devices_.end(), [&](auto& d) { return d->path == path; })) continue;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        std::array<unsigned char, (KEY_CNT+7)/8> keys{};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys.data()) < 0 ||
            !bit(keys, KEY_A) || !bit(keys, KEY_Z) || !bit(keys, KEY_ENTER) || !bit(keys, KEY_SPACE)) { close(fd); continue; }
        auto device = std::make_unique<Device>();
        device->fd = fd; device->path = path;
        device->translator = std::make_unique<KeyboardTranslator>(keymap_, compose_, group_);
        devices_.push_back(std::move(device));
    }
    if (devices_.empty() && !warned_) {
        std::cerr << "No accessible native keyboard; grant this user seat/input access or use ghostty-xrctl type\n";
        warned_ = true;
    }
}
std::vector<SDL_Event> NativeKeyboard::pump() {
    std::vector<SDL_Event> events;
    if (!desired_) return events;
    if (std::chrono::steady_clock::now()-scanned_ > std::chrono::seconds(1)) scan();
    for (auto& device : devices_) {
        input_event input[64];
        for (int batch = 0; batch < 16; ++batch) {
            auto bytes = read(device->fd, input, sizeof(input));
            if (bytes < 0 && errno == ENODEV) { close(device->fd); device->fd = -1; break; }
            if (bytes <= 0) break;
            if (!device->grabbed) continue; // Discard pre-focus events; never replay other apps' input.
            for (size_t i = 0; i < size_t(bytes)/sizeof(input_event); ++i) {
                auto& e = input[i];
                if (e.type == EV_KEY && e.value >= 0 && e.value <= 2) {
                    auto translated = device->translator->key(e.code, e.value);
                    events.insert(events.end(), translated.begin(), translated.end());
                }
            }
        }
        if (device->fd < 0 || device->grabbed) continue;
        std::array<unsigned char, (KEY_CNT+7)/8> pressed{};
        // Let the desktop see releases of the activation chord before taking
        // exclusive focus, avoiding stuck Meta/Ctrl modifiers in other apps.
        if (ioctl(device->fd, EVIOCGKEY(sizeof(pressed)), pressed.data()) >= 0 &&
            std::all_of(pressed.begin(), pressed.end(), [](auto b) { return b == 0; }) && ioctl(device->fd, EVIOCGRAB, 1) == 0) {
            device->grabbed = true;
            std::cerr << "Native keyboard focused: " << device->path << '\n';
        }
    }
    std::erase_if(devices_, [](auto& d) { return d->fd < 0; });
    return events;
}
