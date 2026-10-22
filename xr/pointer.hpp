#pragma once
#include <SDL.h>
#include <chrono>
#include <string>
#include <vector>

// Relative mouse input is owned only while the native terminal owns focus.
class NativePointer {
    struct Device { int fd; bool grabbed; std::string path; };
    std::vector<Device> devices_;
    std::chrono::steady_clock::time_point scanned_{};
    bool focused_ = false;
    int width_, height_;
    void scan();
public:
    int x, y;
    Uint32 buttons = 0;
    bool used = false;
    NativePointer(int width, int height) : width_(width), height_(height), x(width/2), y(height/2) {}
    ~NativePointer();
    void focus(bool enabled);
    void resize(int width, int height) { width_ = width; height_ = height; x = width/2; y = height/2; }
    std::vector<SDL_Event> pump();
    void translate(unsigned type, unsigned code, int value, std::vector<SDL_Event>& events);
};
