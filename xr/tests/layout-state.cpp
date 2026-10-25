#include "layout-state.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() try {
    char temporary[] = "/tmp/ghostty-xr-layout-XXXXXX";
    require(mkdtemp(temporary), "Cannot create test directory");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::filesystem::remove_all(path); }
    } cleanup{temporary};
    const auto path = cleanup.path/"config/layout.json";
    const OverlaySettings first{{1.4f, .8f, .25f, -.1f, -1.6f}, .35f, .7f};
    const OverlaySettings second{{1.6f, .9f, -.2f, .2f, -2.f}, 0.f, 1.f};
    LayoutState state(path);
    require(!state.load(), "Missing layout must use defaults");
    state.changed(first);
    state.tick();
    require(!std::filesystem::exists(path), "Changes must be debounced");
    state.flush();
    require(state.load() == first, "Position, dimensions and both opacity values must survive a new load");
    struct stat st{};
    require(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600, "Settings must be private");
    state.changed(first);
    state.changed(second);
    state.flush();
    require(LayoutState(path).load() == second, "Last adjustment must replace the previous settings");
    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path()))
        require(entry.path() == path, "Temporary files must not remain after saving");
    for (const char* invalid : {"{", "{\"version\":2}", "{\"version\":1,\"width\":1}",
            "{\"version\":1,\"width\":0,\"height\":1,\"x\":0,\"y\":0,\"z\":-1}",
            "{\"version\":1,\"width\":1,\"height\":1,\"x\":0,\"y\":0,\"z\":1}"}) {
        { std::ofstream out(path); out << invalid; }
        require(!state.load(), "Corrupt, incomplete or out-of-range settings must be ignored");
    }
    { std::ofstream out(path); out << R"({"version":1,"width":1.4,"height":0.8,"x":0.25,"y":-0.1,"z":-1.6})"; }
    auto legacy = state.load();
    require(legacy && legacy->layout == first.layout && legacy->opacity == 0 && legacy->styledOpacity == 1,
        "Existing geometry-only layouts must retain position and size with default transparency");
    for (const char* field : {"background_opacity", "styled_background_opacity"}) {
        for (const char* value : {"-0.1", "1.1", "null", "\"invalid\""}) {
            { std::ofstream out(path); out << "{\"version\":1,\"width\":1,\"height\":1,\"x\":0,\"y\":0,\"z\":-1,\"" << field << "\":" << value << '}'; }
            require(!state.load(), "Invalid transparency must be ignored");
        }
    }
    state.changed(first); state.flush();
    auto invalidOpacity = first; invalidOpacity.opacity = std::numeric_limits<float>::quiet_NaN();
    state.changed(invalidOpacity);
    bool refusedOpacity = false;
    try { state.flush(); } catch (const std::exception&) { refusedOpacity = true; }
    require(refusedOpacity && state.load() == first, "Invalid opacity must preserve the last valid layout");
    auto invalid = first; invalid.layout.x = std::numeric_limits<float>::infinity();
    state.changed(invalid);
    bool refused = false;
    try { state.flush(); } catch (const std::exception&) { refused = true; }
    require(refused && state.load() == first, "Invalid save must preserve the last valid layout");
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(cleanup.path);
    LayoutState relative("relative.json"); relative.changed(second); relative.flush();
    require(relative.load() == second, "Relative --layout-file paths must work");
    std::filesystem::current_path(previous);
    std::cout << "Layout persistence, replacement, corrupt input, bounds and relative paths passed\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
