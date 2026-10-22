#include "xr.hpp"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <algorithm>

static void xrcheck(XrResult r, const char* op) {
    if (XR_FAILED(r)) throw std::runtime_error(std::string(op) + " failed: " + std::to_string(r));
}
#define XR(call) xrcheck(call, #call)
static std::vector<XrExtensionProperties> extensions() {
    uint32_t count = 0;
    XR(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr));
    std::vector<XrExtensionProperties> ext(count, {XR_TYPE_EXTENSION_PROPERTIES});
    XR(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, ext.data()));
    return ext;
}
static bool has(const std::vector<XrExtensionProperties>& exts, const char* name) {
    return std::any_of(exts.begin(), exts.end(), [name](auto& e) { return !strcmp(e.extensionName, name); });
}
static XrInstance create(const std::vector<const char*>& exts) {
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy(info.applicationInfo.applicationName, "FLTerm XR");
    info.applicationInfo.applicationVersion = 1;
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    info.enabledExtensionCount = exts.size(); info.enabledExtensionNames = exts.data();
    XrInstance result{};
    XR(xrCreateInstance(&info, &result));
    return result;
}
void XrOverlay::probe() {
    auto exts = extensions();
    for (auto name : {XR_EXTX_OVERLAY_EXTENSION_NAME, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME,
                      XR_MNDX_EGL_ENABLE_EXTENSION_NAME, "XR_FB_passthrough", "XR_HTC_passthrough"})
        std::cout << name << ": " << (has(exts, name) ? "yes" : "no") << '\n';
    auto instance = create({});
    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    XR(xrGetInstanceProperties(instance, &props));
    std::cout << "Runtime: " << props.runtimeName << '\n';
    XrSystemGetInfo get{XR_TYPE_SYSTEM_GET_INFO}; get.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system{};
    auto result = xrGetSystem(instance, &get, &system);
    if (XR_SUCCEEDED(result)) {
        XrSystemProperties p{XR_TYPE_SYSTEM_PROPERTIES};
        XR(xrGetSystemProperties(instance, system, &p));
        std::cout << "Headset: " << p.systemName << '\n';
        uint32_t count = 0;
        XR(xrEnumerateEnvironmentBlendModes(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr));
        std::vector<XrEnvironmentBlendMode> modes(count);
        XR(xrEnumerateEnvironmentBlendModes(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, modes.data()));
        for (auto mode : modes) std::cout << "Blend: " << (mode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE ? "opaque" : mode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND ? "alpha" : "additive") << '\n';
    }
    xrDestroyInstance(instance);
    xrcheck(result, "xrGetSystem");
}
XrOverlay::XrOverlay(const NativeGraphics& graphics, int w, int h, float m, float d, float v,
                     bool standalone, uint32_t placement, const char* blendName)
    : width(w), height(h), meters(m), distance(d), vertical(v) {
    auto exts = extensions();
    if (!has(exts, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) throw std::runtime_error("Runtime lacks XR_KHR_opengl_enable");
    if (!standalone && !has(exts, XR_EXTX_OVERLAY_EXTENSION_NAME))
        throw std::runtime_error("Runtime lacks XR_EXTX_overlay; overlay mode cannot coexist with other XR apps. --standalone is available explicitly.");
    if (!has(exts, XR_MNDX_EGL_ENABLE_EXTENSION_NAME)) throw std::runtime_error("Runtime lacks XR_MNDX_egl_enable, required for windowless rendering");
    std::vector<const char*> enabled{XR_KHR_OPENGL_ENABLE_EXTENSION_NAME, XR_MNDX_EGL_ENABLE_EXTENSION_NAME};
    if (!standalone) enabled.push_back(XR_EXTX_OVERLAY_EXTENSION_NAME);
    instance = create(enabled);
    XrSystemGetInfo get{XR_TYPE_SYSTEM_GET_INFO}; get.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR(xrGetSystem(instance, &get, &system));
    PFN_xrGetOpenGLGraphicsRequirementsKHR requirementsFn{};
    XR(xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&requirementsFn)));
    XrGraphicsRequirementsOpenGLKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    XR(requirementsFn(instance, system, &requirements));
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor);
    auto version = XR_MAKE_VERSION(major, minor, 0);
    if (version < requirements.minApiVersionSupported || version > requirements.maxApiVersionSupported)
        throw std::runtime_error("OpenGL context is outside the runtime's supported version range");
    XrSessionCreateInfoOverlayEXTX overlay{XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX};
    overlay.sessionLayersPlacement = placement;
    XrGraphicsBindingEGLMNDX binding{XR_TYPE_GRAPHICS_BINDING_EGL_MNDX};
    binding.next = standalone ? nullptr : &overlay;
    binding.getProcAddress = reinterpret_cast<PFN_xrEglGetProcAddressMNDX>(eglGetProcAddress);
    binding.display = graphics.display; binding.config = graphics.config; binding.context = graphics.context;
    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO}; info.next = &binding; info.systemId = system;
    XR(xrCreateSession(instance, &info, &session));
    XrReferenceSpaceCreateInfo ref{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    ref.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW; ref.poseInReferenceSpace.orientation.w = 1;
    XR(xrCreateReferenceSpace(session, &ref, &space));
    uint32_t n = 0;
    XR(xrEnumerateEnvironmentBlendModes(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &n, nullptr));
    std::vector<XrEnvironmentBlendMode> modes(n);
    XR(xrEnumerateEnvironmentBlendModes(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, n, &n, modes.data()));
    blend = !strcmp(blendName, "alpha") ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND :
            !strcmp(blendName, "additive") ? XR_ENVIRONMENT_BLEND_MODE_ADDITIVE : XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if (!strcmp(blendName, "auto")) {
        blend = std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) != modes.end()
            ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND : modes.at(0);
    }
    if (std::find(modes.begin(), modes.end(), blend) == modes.end()) throw std::runtime_error("Requested environment blend mode is unsupported; see --probe");
    resize(width, height);
    std::cerr << "XR overlay ready; " << width << 'x' << height << ", " << meters << "m wide, " << distance << "m away\n";
}
void XrOverlay::resize(int w, int h) {
    uint32_t n = 0;
    XR(xrEnumerateSwapchainFormats(session, 0, &n, nullptr));
    std::vector<int64_t> formats(n);
    XR(xrEnumerateSwapchainFormats(session, n, &n, formats.data()));
    int64_t format = GL_SRGB8_ALPHA8;
    if (std::find(formats.begin(), formats.end(), format) == formats.end()) format = GL_RGBA8;
    if (std::find(formats.begin(), formats.end(), format) == formats.end()) throw std::runtime_error("No supported RGBA swapchain format");
    XrSwapchain next{};
    XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    sc.format = format; sc.sampleCount = 1; sc.width = w; sc.height = h;
    sc.faceCount = 1; sc.arraySize = 1; sc.mipCount = 1;
    XR(xrCreateSwapchain(session, &sc, &next));
    try {
        XR(xrEnumerateSwapchainImages(next, 0, &n, nullptr));
        std::vector<XrSwapchainImageOpenGLKHR> nextImages(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
        XR(xrEnumerateSwapchainImages(next, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader*>(nextImages.data())));
        if (swapchain) xrDestroySwapchain(swapchain);
        swapchain = next; images.swap(nextImages); width = w; height = h;
    } catch (...) { xrDestroySwapchain(next); throw; }
}
XrOverlay::~XrOverlay() {
    if (swapchain) xrDestroySwapchain(swapchain);
    if (space) xrDestroySpace(space);
    if (session) xrDestroySession(session);
    if (instance) xrDestroyInstance(instance);
    std::cerr << "XR frames=" << frames << " visible=" << visibleFrames << " hidden=" << hiddenFrames << '\n';
}
void XrOverlay::events() {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult result;
    while ((result = xrPollEvent(instance, &event)) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto& change = reinterpret_cast<XrEventDataSessionStateChanged&>(event);
            if (change.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO}; begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                XR(xrBeginSession(session, &begin)); running = true;
            } else if (change.state == XR_SESSION_STATE_STOPPING) {
                running = false; XR(xrEndSession(session));
            } else if (change.state == XR_SESSION_STATE_EXITING || change.state == XR_SESSION_STATE_LOSS_PENDING) quit = true;
        } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) quit = true;
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    if (result != XR_EVENT_UNAVAILABLE) xrcheck(result, "xrPollEvent");
}
void XrOverlay::frame(bool visible, const std::vector<uint8_t>& pixels) {
    if (!running) return;
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state{XR_TYPE_FRAME_STATE};
    XR(xrWaitFrame(session, &wait, &state));
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO}; XR(xrBeginFrame(session, &begin));
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad.space = space; quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.pose.orientation.w = 1; quad.pose.position.z = -distance; quad.pose.position.y = vertical; quad.pose.position.x = horizontal;
    quad.size = {meters, panelHeight > 0 ? panelHeight : meters*height/width};
    quad.subImage.swapchain = swapchain; quad.subImage.imageRect.extent = {width, height};
    const XrCompositionLayerBaseHeader* layer = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
    bool submit = visible && state.shouldRender;
    if (submit) {
        uint32_t index = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XR(xrAcquireSwapchainImage(swapchain, &acquire, &index));
        XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; imageWait.timeout = XR_INFINITE_DURATION;
        XR(xrWaitSwapchainImage(swapchain, &imageWait));
        glBindTexture(GL_TEXTURE_2D, images[index].image);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glFlush();
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        XR(xrReleaseSwapchainImage(swapchain, &release));
    }
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO}; end.displayTime = state.predictedDisplayTime;
    end.environmentBlendMode = blend; end.layerCount = submit ? 1 : 0; end.layers = submit ? &layer : nullptr;
    XR(xrEndFrame(session, &end));
    ++frames; if (submit) ++visibleFrames; if (!visible) ++hiddenFrames;
}
