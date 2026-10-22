// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
struct Control {
  int fd = -1;
  std::string path;
  explicit Control(bool desktop) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime || !*runtime)
      throw std::runtime_error("XDG_RUNTIME_DIR is required");
    auto dir = std::string(runtime) + "/flterm-xr";
    if (mkdir(dir.c_str(), 0700) && errno != EEXIST)
      throw std::runtime_error("Cannot create control directory");
    struct stat st{};
    if (lstat(dir.c_str(), &st) || !S_ISDIR(st.st_mode) ||
        st.st_uid != getuid() || (st.st_mode & 077))
      throw std::runtime_error(
          "Control directory must be private and owned by you");
    path = dir + (desktop ? "/wayland-" + std::to_string(getpid()) + ".sock"
                          : "/control.sock");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path))
      throw std::runtime_error("Control socket path is too long");
    memcpy(address.sun_path, path.c_str(), path.size() + 1);
    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
      throw std::runtime_error("Cannot create control socket");
    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address))) {
      close(fd);
      fd = -1;
      throw std::runtime_error("Cannot bind " + path +
                               "; use flterm-xr-launch to reuse an overlay");
    }
  }
  ~Control() {
    if (fd >= 0) {
      close(fd);
      unlink(path.c_str());
    }
  }
  Control(const Control &) = delete;
};
