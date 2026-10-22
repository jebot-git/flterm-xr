#pragma once
#include <future>
#include <deque>
#include <optional>
#include <string>

// SDL handles focused window selections. Windowless overlays use Wayland data-control.
class Clipboard {
    bool windowed_;
    std::future<std::optional<std::string>> operation_;
    struct Request { bool copy, primary; std::string text; };
    std::deque<Request> requests_;
    void begin(Request request);
public:
    explicit Clipboard(bool windowed);
    bool busy() const { return operation_.valid() || !requests_.empty(); }
    void copy(const std::string& text, bool primary = false);
    void paste(bool primary = false);
    std::optional<std::string> poll();
};
