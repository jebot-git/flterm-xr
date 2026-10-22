#include "pointer.hpp"
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <filesystem>

NativePointer::~NativePointer() { focus(false); }
void NativePointer::focus(bool enabled) {
    if (focused_ == enabled) return;
    focused_ = enabled;
    if (!enabled) {
        for (auto& device : devices_) {
            if (device.grabbed) ioctl(device.fd, EVIOCGRAB, 0);
            close(device.fd);
        }
        devices_.clear(); buttons = 0; used = false;
    } else scanned_ = {};
}
void NativePointer::scan() {
    scanned_ = std::chrono::steady_clock::now();
    std::error_code error;
    for (auto& item : std::filesystem::directory_iterator("/dev/input", error)) {
        auto path = item.path().string();
        if (!item.path().filename().string().starts_with("event") ||
            std::any_of(devices_.begin(), devices_.end(), [&](auto& d) { return d.path == path; })) continue;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        std::array<unsigned char, (KEY_CNT+7)/8> keys{};
        unsigned long relative = 0;
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys.data()) < 0 ||
            ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relative)), &relative) < 0 ||
            !(keys[BTN_LEFT/8] & (1u << (BTN_LEFT%8))) ||
            !(relative & (1ul << REL_X)) || !(relative & (1ul << REL_Y))) { close(fd); continue; }
        devices_.push_back({fd, false, path});
    }
}
void NativePointer::translate(unsigned type, unsigned code, int value, std::vector<SDL_Event>& events) {
    SDL_Event event{};
    if (type == EV_REL && (code == REL_X || code == REL_Y)) {
        if (code == REL_X) x = std::clamp(x+value, 0, width_-1);
        else y = std::clamp(y+value, 0, height_-1);
        event.type = SDL_MOUSEMOTION; event.motion.x = x; event.motion.y = y; event.motion.state = buttons;
        used = true;
    } else if (type == EV_REL && (code == REL_WHEEL || code == REL_HWHEEL)) {
        event.type = SDL_MOUSEWHEEL;
        event.wheel.y = code == REL_WHEEL ? value : 0;
        event.wheel.x = code == REL_HWHEEL ? value : 0;
    } else if (type == EV_KEY && (code == BTN_LEFT || code == BTN_RIGHT || code == BTN_MIDDLE) && (value == 0 || value == 1)) {
        auto button = code == BTN_LEFT ? SDL_BUTTON_LEFT : code == BTN_RIGHT ? SDL_BUTTON_RIGHT : SDL_BUTTON_MIDDLE;
        if (value) buttons |= SDL_BUTTON(button); else buttons &= ~SDL_BUTTON(button);
        event.type = value ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.button = button; event.button.state = value ? SDL_PRESSED : SDL_RELEASED;
        event.button.x = x; event.button.y = y;
        used = true;
    } else return;
    events.push_back(event);
}
std::vector<SDL_Event> NativePointer::pump() {
    std::vector<SDL_Event> events;
    if (!focused_) return events;
    if (std::chrono::steady_clock::now()-scanned_ > std::chrono::seconds(1)) scan();
    for (auto& device : devices_) {
        input_event input[64];
        for (int batch=0; batch<8; ++batch) {
            auto bytes = read(device.fd, input, sizeof(input));
            if (bytes < 0 && errno == ENODEV) { close(device.fd); device.fd = -1; break; }
            if (bytes <= 0) break;
            if (device.grabbed) for (size_t i=0; i<size_t(bytes)/sizeof(input_event); ++i)
                translate(input[i].type, input[i].code, input[i].value, events);
        }
        if (device.fd < 0 || device.grabbed) continue;
        std::array<unsigned char, (KEY_CNT+7)/8> pressed{};
        // Do not consume a release belonging to the previously focused application.
        if (ioctl(device.fd, EVIOCGKEY(sizeof(pressed)), pressed.data()) >= 0 &&
            std::all_of(pressed.begin(), pressed.end(), [](auto b) { return b == 0; }) &&
            ioctl(device.fd, EVIOCGRAB, 1) == 0) device.grabbed = true;
    }
    std::erase_if(devices_, [](auto& device) { return device.fd < 0; });
    return events;
}
