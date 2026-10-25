#pragma once
#include "layout.hpp"
#include <filesystem>
#include <optional>
#include <chrono>

// Physical quad dimensions retain the current scale, independently of text pixels.
struct OverlaySettings {
    PanelLayout layout;
    float opacity = 0, styledOpacity = 1;
    bool operator==(const OverlaySettings&) const = default;
};

class LayoutState {
    std::filesystem::path path_;
    std::optional<OverlaySettings> pending_;
    std::chrono::steady_clock::time_point changed_;
public:
    explicit LayoutState(std::filesystem::path path = defaultPath()) : path_(std::move(path)) {}
    static std::filesystem::path defaultPath();
    std::optional<OverlaySettings> load() const;
    void changed(const OverlaySettings& settings);
    void tick();
    void flush();
};
