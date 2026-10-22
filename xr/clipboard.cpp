#include "clipboard.hpp"
#include <SDL.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <algorithm>
extern char** environ;
Clipboard::Clipboard(bool windowed) : windowed_(windowed) {
    // SDL's Wayland selection can be empty after an unfocused data-control update.
    // Use the same asynchronous seat clipboard transport as the native overlay.
    auto driver = windowed ? SDL_GetCurrentVideoDriver() : nullptr;
    if (driver && std::strcmp(driver, "wayland") == 0) windowed_ = false;
}

static std::optional<std::string> transfer(bool copy, bool primary, const std::string& text) {
    constexpr size_t limit = 1024*1024;
    if (text.size() > limit) return {};
    FILE* input = copy ? tmpfile() : nullptr;
    if (copy && !input) return {};
    if (copy) {
        fcntl(fileno(input), F_SETFD, FD_CLOEXEC);
        if (fwrite(text.data(), 1, text.size(), input) != text.size()) { fclose(input); return {}; }
        rewind(input);
    }
    int output[2] = {-1, -1};
    if (!copy && pipe2(output, O_CLOEXEC)) return {};
    std::vector<std::string> arguments = {"/usr/bin/timeout", "2s", copy ? "/usr/bin/wl-copy" : "/usr/bin/wl-paste"};
    if (primary) arguments.push_back("--primary");
    if (copy) { arguments.push_back("--type"); arguments.push_back("text/plain;charset=utf-8"); }
    else arguments.push_back("--no-newline");
    std::vector<char*> argv;
    for (auto& arg : arguments) argv.push_back(arg.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (copy) posix_spawn_file_actions_adddup2(&actions, fileno(input), STDIN_FILENO);
    else posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (!copy) {
        posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, output[0]);
        posix_spawn_file_actions_addclose(&actions, output[1]);
    } else posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t child = -1;
    int result = posix_spawn(&child, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (input) fclose(input);
    if (!copy) close(output[1]);
    std::string data;
    if (!result && !copy) {
        char buffer[8192];
        for (;;) {
            auto n = read(output[0], buffer, sizeof(buffer));
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            if (data.size()+size_t(n) > limit) { result = 1; break; }
            data.append(buffer, size_t(n));
        }
    }
    if (!copy) close(output[0]);
    int status = 0;
    if (child > 0) while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    if (result || !WIFEXITED(status) || WEXITSTATUS(status)) {
        std::cerr << "Clipboard transfer failed; install wl-clipboard and connect to a compositor with data-control support.\n";
        return {};
    }
    return copy ? std::nullopt : std::optional<std::string>(std::move(data));
}
void Clipboard::copy(const std::string& text, bool primary) {
    if (text.empty() || text.size() > 1024*1024) return;
    // Coalesce pending copies, while preserving the order of clipboard reads.
    if (!requests_.empty() && requests_.back().copy && requests_.back().primary == primary)
        requests_.back().text = text;
    else {
        if (requests_.size() >= 8) {
            auto previous = std::find_if(requests_.begin(), requests_.end(), [](auto& r) { return r.copy; });
            if (previous != requests_.end()) requests_.erase(previous);
        }
        requests_.push_back({true, primary, text});
    }
}
void Clipboard::paste(bool primary) { requests_.push_back({false, primary, {}}); }
void Clipboard::begin(Request request) {
    if (request.copy && windowed_) {
        if ((request.primary ? SDL_SetPrimarySelectionText(request.text.c_str()) : SDL_SetClipboardText(request.text.c_str())))
            std::cerr << "Clipboard: " << SDL_GetError() << '\n';
    } else if (!request.copy && windowed_) {
        char* text = request.primary ? SDL_GetPrimarySelectionText() : SDL_GetClipboardText();
        std::string value = text ? text : "";
        SDL_free(text);
        std::promise<std::optional<std::string>> ready;
        ready.set_value(std::move(value));
        operation_ = ready.get_future();
    } else operation_ = std::async(std::launch::async, transfer, request.copy, request.primary, std::move(request.text));
}
std::optional<std::string> Clipboard::poll() {
    std::optional<std::string> result;
    if (operation_.valid()) {
        if (operation_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return {};
        result = operation_.get();
    }
    if (!requests_.empty()) {
        auto request = std::move(requests_.front()); requests_.pop_front();
        begin(std::move(request));
    }
    return result;
}
