// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "host.h"
#include <string>
#include <vector>
class PtyHost : public HOST {
  int fd_ = -1;
  pid_t child_ = -1;
  std::vector<std::string> command_;
  std::string queued_;
  size_t sent_ = 0;

public:
  explicit PtyHost(std::vector<std::string> command)
      : command_(std::move(command)) {}
  ~PtyHost() override;
  const char *name() override { return "FLTerm XR"; }
  int type() override { return HOST_PIPE; }
  int live() override { return child_ > 0; }
  void connect() override;
  int write(const char *, int) override;
  void send_size(int, int) override;
  void disconn() override;
  bool pump();
};
