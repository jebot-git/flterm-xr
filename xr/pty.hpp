// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "host.h"
#include <string>
#include <vector>
class PtyHost : public HOST {
  int fd_ = -1;
  pid_t child_ = -1;
  std::vector<std::string> command_;
  std::string queued_, realm_;
  size_t sent_ = 0;
  int cell_width_=8, cell_height_=16, cols_=100, rows_=30;

public:
  explicit PtyHost(std::vector<std::string> command, std::string realm = "DE")
      : command_(std::move(command)), realm_(std::move(realm)) {}
  ~PtyHost() override;
  const char *name() override { return "FLTerm XR"; }
  int type() override { return HOST_PIPE; }
  int live() override { return child_ > 0; }
  void connect() override;
  int write(const char *, int) override;
  void send_size(int, int) override;
  void cellSize(int width,int height) { cell_width_=width;cell_height_=height;send_size(cols_,rows_); }
  void disconn() override;
  bool pump();
};
