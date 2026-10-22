#pragma once
#include "layout.hpp"
#include <filesystem>
#include <optional>
#include <chrono>

class LayoutState {
    std::filesystem::path path_;
    std::optional<PanelLayout> pending_;
    std::chrono::steady_clock::time_point changed_;
public:
    explicit LayoutState(std::filesystem::path path = defaultPath()) : path_(std::move(path)) {}
    static std::filesystem::path defaultPath();
    std::optional<PanelLayout> load() const;
    void changed(const PanelLayout& layout);
    void tick();
    void flush();
};
