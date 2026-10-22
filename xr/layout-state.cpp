#include "layout-state.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>

static bool valid(const PanelLayout& p) {
    return std::isfinite(p.width) && p.width > 0 && p.width <= 10 &&
        std::isfinite(p.height) && p.height > 0 && p.height <= 10 &&
        std::isfinite(p.x) && std::abs(p.x) <= 10 && std::isfinite(p.y) && std::abs(p.y) <= 10 &&
        std::isfinite(p.z) && p.z <= -.2f && p.z >= -20;
}
std::filesystem::path LayoutState::defaultPath() {
    if (auto config = getenv("XDG_CONFIG_HOME"); config && *config) return std::filesystem::path(config)/"flterm-xr/layout.json";
    if (auto home = getenv("HOME"); home && *home) return std::filesystem::path(home)/".config/flterm-xr/layout.json";
    throw std::runtime_error("HOME or XDG_CONFIG_HOME is required to save the XR layout");
}
std::optional<PanelLayout> LayoutState::load() const {
    try {
        if (!std::filesystem::exists(path_)) return std::nullopt;
        std::ifstream input(path_);
        auto j = nlohmann::json::parse(input);
        if (j.at("version").get<int>() != 1) throw std::runtime_error("unsupported version");
        PanelLayout p{j.at("width").get<float>(), j.at("height").get<float>(),
            j.at("x").get<float>(), j.at("y").get<float>(), j.at("z").get<float>()};
        if (!valid(p)) throw std::runtime_error("invalid position or size");
        return p;
    } catch (const std::exception& e) {
        std::cerr << "Ignoring invalid XR layout " << path_ << ": " << e.what() << '\n';
        return std::nullopt;
    }
}
void LayoutState::changed(const PanelLayout& layout) {
    pending_ = layout;
    changed_ = std::chrono::steady_clock::now();
}
void LayoutState::tick() {
    if (pending_ && std::chrono::steady_clock::now()-changed_ >= std::chrono::milliseconds(250)) flush();
}
void LayoutState::flush() {
    if (!pending_) return;
    if (!valid(*pending_)) throw std::runtime_error("Refusing to save an invalid XR layout");
    auto& p = *pending_;
    nlohmann::json j{{"version",1},{"width",p.width},{"height",p.height},{"x",p.x},{"y",p.y},{"z",p.z}};
    const auto directory = path_.has_parent_path() ? path_.parent_path() : std::filesystem::path(".");
    std::filesystem::create_directories(directory);
    std::string temporary = path_.string()+".tmp-XXXXXX";
    int fd = mkstemp(temporary.data());
    if (fd < 0) throw std::runtime_error("Cannot create layout file: " + path_.string());
    try {
        auto data = j.dump(2)+"\n";
        size_t offset = 0;
        while (offset < data.size()) {
            auto n = write(fd, data.data()+offset, data.size()-offset);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("Cannot write XR layout");
            offset += n;
        }
        if (fsync(fd)) throw std::runtime_error("Cannot sync XR layout");
        close(fd); fd = -1;
        std::filesystem::rename(temporary, path_);
        // Persist the rename as well as the contents, including across power loss.
        int directoryFd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directoryFd < 0) throw std::runtime_error("Cannot open XR layout directory");
        int result = fsync(directoryFd);
        close(directoryFd);
        if (result) throw std::runtime_error("Cannot sync XR layout directory");
        pending_.reset();
    } catch (...) {
        if (fd >= 0) close(fd);
        unlink(temporary.c_str());
        throw;
    }
}
