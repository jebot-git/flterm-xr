// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>
#include <cstdint>

// A release can edit only the terminal where its matching press began.
struct CursorClick {
  uint64_t tab = 0;
  int x = 0, y = 0;
  bool dragged = false, enabled = false;
  void begin(uint64_t identity, int px, int py, bool plain) {
    tab = identity; x = px; y = py; dragged = false; enabled = plain;
  }
  void motion(int px, int py) {
    dragged |= std::abs(px - x) > 3 || std::abs(py - y) > 3;
  }
  bool finish(uint64_t identity, int px, int py) {
    motion(px, py);
    bool click = tab && tab == identity && enabled && !dragged;
    tab = 0;
    return click;
  }
  void cancel() { tab = 0; }
};
