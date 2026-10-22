#pragma once
#include <filesystem>
#include <optional>
#include <string>

struct FontChoice {
    std::string path;
    int pixels = 24;
    bool operator==(const FontChoice&) const = default;
};
// Reads Dome's atomically saved appearance settings only inside a Dome session.
// Each command-line option independently overrides its inherited counterpart.
class DomeFont {
    std::filesystem::path settings_;
    std::string explicitFont_;
    int explicitSize_;
public:
    DomeFont(std::string font, int pixels, bool sizeExplicit);
    std::optional<FontChoice> read() const;
};
