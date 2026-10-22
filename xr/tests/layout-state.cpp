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
    const PanelLayout first{1.4f, .8f, .25f, -.1f, -1.6f};
    const PanelLayout second{1.6f, .9f, -.2f, .2f, -2.f};
    LayoutState state(path);
    require(!state.load(), "Missing layout must use defaults");
    state.changed(first);
    state.tick();
    require(!std::filesystem::exists(path), "Changes must be debounced");
    state.flush();
    require(state.load() == first, "Position and dimensions must survive a new load");
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
    state.changed(first); state.flush();
    auto invalid = first; invalid.x = std::numeric_limits<float>::infinity();
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
