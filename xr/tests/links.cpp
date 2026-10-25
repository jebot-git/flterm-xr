// SPDX-License-Identifier: GPL-3.0-or-later
#include "renderer.hpp"
#include <cassert>
#include <clocale>
#include <iostream>
int main() {
  setlocale(LC_ALL, "C.UTF-8");
  unsetenv("DISPLAY");
  unsetenv("WAYLAND_DISPLAY");
  Fl_Term t(0, 0, 640, 320, nullptr, true);
  t.grid(40, 6);
  t.callback([](Fl_Widget *, void *) {});
  std::string osc =
      "\033]8;id=a;https://example.com/日本語\033\\LABEL\033]8;;\007 plain";
  for (char c : osc)
    t.puts(&c, 1);
  assert(t.linkAt(0, 0) == "https://example.com/日本語" &&
         t.linkAt(4, 0) == t.linkAt(0, 0));
  assert(t.linkAt(6, 0).empty());
  t.disp("\r\033[2C\033[X");
  assert(t.linkAt(2, 0).empty() && !t.linkAt(3, 0).empty());
  t.disp("\033[?1049hhttps://alternate.example\033[?1049l");
  assert(t.linkAt(0, 0) == "https://example.com/日本語");
  t.disp("\r\n(https://example.com/a_(b)).\r\nwww.example.com "
         "test@example.com\r\n");
  assert(t.linkAt(5, 1) == "https://example.com/a_(b)");
  assert(t.linkAt(26, 1).empty());
  assert(t.linkAt(5, 2) == "https://www.example.com");
  assert(t.linkAt(20, 2) == "mailto:test@example.com");
  t.disp("\033]8;;javascript:alert(1)\007BAD\033]8;;\007");
  assert(t.linkAt(0, 3).empty());
  t.disp("\033[2J\033[Hhttps://example.com/abcdefghijklmnopqrstuvwxyz");
  assert(t.linkAt(2, 1) == "https://example.com/abcdefghijklmnopqrstuvwxyz");
  t.disp("\r\n\r\n\r\n\r\n\r\n");
  t.scrollRows(-10000);
  assert(t.linkAt(0, 0) == "https://example.com/abcdefghijklmnopqrstuvwxyz");
  t.selectAll();
  assert(t.selectionText().find("https://example.com") != std::string::npos);
  std::cout << "PASS: fragmented OSC 8, UTF-8 targets, closing, erase, "
               "alternate screen, detected URL/email, balanced punctuation, "
               "soft wraps and scrollback\n";
}
