#pragma once
#define XR_USE_PLATFORM_EGL
#define XR_USE_GRAPHICS_API_OPENGL
#include "graphics.hpp"
#include <GL/gl.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstdint>
#include <vector>

struct XrOverlay {
    XrInstance instance{};
    XrSystemId system{};
    XrSession session{};
    XrSpace space{};
    XrSwapchain swapchain{};
    std::vector<XrSwapchainImageOpenGLKHR> images;
    XrEnvironmentBlendMode blend{};
    bool running = false, quit = false;
    uint64_t frames = 0, visibleFrames = 0, hiddenFrames = 0;
    int width, height;
    float meters, distance, vertical;
    float horizontal = 0, panelHeight = 0;
    XrOverlay(const NativeGraphics& graphics, int width, int height, float meters, float distance,
              float vertical, bool standalone, uint32_t placement, const char* blendName);
    ~XrOverlay();
    void events();
    void resize(int width, int height);
    void frame(bool visible, const std::vector<uint8_t>& pixels);
    static void probe();
};
