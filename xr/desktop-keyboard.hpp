// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "keyboard.hpp"
#include <deque>
#include <map>
struct wl_display;
struct wl_registry;
struct wl_seat;
struct wl_keyboard;
// Observe the compositor's actual keymap/physical keys while FLTK owns input,
// composition and repeat dispatch. No second display connection or input grab.
class DesktopKeyboard {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  explicit DesktopKeyboard(wl_display *display);
  ~DesktopKeyboard();
  bool ready() const;
  KeyInput resolve(unsigned symbol, const SDL_KeyboardEvent &event,
                   const std::string &text);
};
