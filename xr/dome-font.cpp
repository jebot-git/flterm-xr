#include "dome-font.hpp"
#include <fontconfig/fontconfig.h>
#include "bdf_font.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>

DomeFont::DomeFont(std::string font, int pixels, bool sizeExplicit)
    : explicitFont_(std::move(font)), explicitSize_(sizeExplicit ? pixels : 0) {
    const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    std::istringstream names(desktop ? desktop : "");
    std::string name;
    bool dome = false;
    while (std::getline(names, name, ':')) if (name == "Dome" || name == "dome" || name == "DomeD") dome = true;
    if (!dome || (!explicitFont_.empty() && explicitSize_)) return;
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (config && *config) settings_ = std::filesystem::path(config) / "dome/appearance.json";
    else if (home && *home) settings_ = std::filesystem::path(home) / ".config/dome/appearance.json";
}
std::optional<FontChoice> DomeFont::read() const {
    if (settings_.empty()) return {};
    try {
        std::error_code ec;
        auto settings = nlohmann::json::object();
        if (std::filesystem::exists(settings_, ec)) {
            if (std::filesystem::file_size(settings_, ec) > 65536 || ec) return {};
            std::ifstream input(settings_);
            settings = nlohmann::json::parse(input);
        } else if (ec) return {};
        std::string id = settings.value("font", "GNU Unifont / Sans-Serif");
        int pixels = settings.value("font_size", 16);
        if (id.empty() || id.size() > 256 || pixels < 4 || pixels > 64) return {};
        if (explicitSize_) pixels = explicitSize_;
        if (!explicitFont_.empty()) return FontChoice{explicitFont_, pixels};
        auto split = id.find(" / ");
        std::string family = id.substr(0, split);
        std::string style = split == std::string::npos ? "" : id.substr(split+3);
        if (family == "GNU Unifont") family = "Unifont";
        auto selected = bdf_fonts::resolve(family, style, pixels, settings.value("font_scale", 1));
        return FontChoice{selected.file, selected.pixels};
    } catch (const std::exception&) { return {}; }
}
