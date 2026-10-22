// SPDX-License-Identifier: GPL-3.0-or-later
#include "pty.hpp"
#include <cerrno>
#include <chrono>
#include <clocale>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <langinfo.h>
#include <pty.h>
#include <sstream>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char **environ;
PtyHost::~PtyHost() {
  disconn();
  if (fd_ >= 0)
    close(fd_);
  if (child_ > 0) {
    for (int i = 0; i < 20; ++i) {
      if (waitpid(child_, nullptr, WNOHANG) == child_) {
        child_ = -1;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (child_ > 0) {
      kill(child_, SIGKILL);
      waitpid(child_, nullptr, 0);
    }
  }
}
void PtyHost::connect() {
  if (child_ > 0)
    return;
  if (fd_ >= 0)
    close(fd_);
  winsize size{};
  size.ws_col = 100;
  size.ws_row = 30;
  // Build argv before fork; the child only performs async-signal-safe setup.
  auto args = command_;
  if (args.empty()) {
    const char *shell = getenv("SHELL");
    args = {shell && *shell ? shell : "/bin/sh", "-i"};
  }
  std::vector<char *> argv;
  for (auto &s : args)
    argv.push_back(s.data());
  argv.push_back(nullptr);
  std::string program = args.front();
  if (program.find('/') == std::string::npos) {
    std::istringstream search(getenv("PATH") ? getenv("PATH")
                                             : "/usr/bin:/bin");
    std::string directory;
    while (std::getline(search, directory, ':')) {
      auto candidate =
          (std::filesystem::path(directory.empty() ? "." : directory) / program)
              .string();
      if (!access(candidate.c_str(), X_OK)) {
        program = candidate;
        break;
      }
    }
  }
  bool utf8 = std::string(nl_langinfo(CODESET)) == "UTF-8";
  std::vector<std::string> environment;
  for (char **entry = environ; *entry; ++entry) {
    std::string value = *entry;
    if (value.starts_with("TERM=") || value.starts_with("COLORTERM=") ||
        (!utf8 && value.starts_with("LC_ALL=")))
      continue;
    environment.push_back(std::move(value));
  }
  environment.emplace_back("TERM=xterm-256color");
  environment.emplace_back("COLORTERM=truecolor");
  if (!utf8)
    environment.emplace_back("LC_ALL=C.UTF-8");
  std::vector<char *> envp;
  for (auto &value : environment)
    envp.push_back(value.data());
  envp.push_back(nullptr);
  child_ = forkpty(&fd_, nullptr, nullptr, &size);
  if (child_ < 0)
    throw std::runtime_error("Cannot create terminal PTY");
  if (child_ == 0) {
    sigset_t signals;
    sigemptyset(&signals);
    sigprocmask(SIG_SETMASK, &signals, nullptr);
    for (int sig : {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE})
      signal(sig, SIG_DFL);
    execve(program.c_str(), argv.data(), envp.data());
    static const char error[] = "FLTerm XR: cannot execute command\r\n";
    auto ignored = ::write(2, error, sizeof(error) - 1);
    (void)ignored;
    _exit(127);
  }
  fcntl(fd_, F_SETFD, FD_CLOEXEC);
  fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
  termios tty{};
  if (!tcgetattr(fd_, &tty)) {
    tty.c_iflag |= IUTF8;
    tcsetattr(fd_, TCSANOW, &tty);
  }
  status(HOST_CONNECTED);
  term_puts("", 0);
}
void PtyHost::disconn() {
  if (child_ > 0)
    kill(-child_, SIGHUP);
}
void PtyHost::send_size(int cols, int rows) {
  if (fd_ < 0)
    return;
  winsize size{};
  size.ws_col = cols;
  size.ws_row = rows;
  ioctl(fd_, TIOCSWINSZ, &size);
}
int PtyHost::write(const char *bytes, int length) {
  if (length <= 0 || queued_.size() - sent_ + length > 4 * 1024 * 1024)
    return -1;
  queued_.append(bytes, length);
  return length;
}
bool PtyHost::pump() {
  bool changed = false;
  while (sent_ < queued_.size()) {
    auto n = ::write(fd_, queued_.data() + sent_, queued_.size() - sent_);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      break;
    sent_ += n;
  }
  if (sent_ == queued_.size()) {
    queued_.clear();
    sent_ = 0;
  }
  char bytes[16384];
  for (int i = 0; i < 64; ++i) {
    auto n = ::read(fd_, bytes, sizeof(bytes));
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      break;
    term_puts(bytes, n);
    changed = true;
  }
  if (child_ > 0 && waitpid(child_, nullptr, WNOHANG) == child_) {
    child_ = -1;
    status(HOST_IDLE);
    changed = true;
  }
  return changed;
}
