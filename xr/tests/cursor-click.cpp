// SPDX-License-Identifier: GPL-3.0-or-later
#include "Fl_Term.h"
#include "click.hpp"
#include "terminal-style.hpp"
#include "renderer.hpp"
#include <cassert>
#include <clocale>
#include <chrono>
#include <thread>
#include <iostream>

struct Capture : HOST {
  std::string input;
  void connect() override {}
  int live() override { return 1; }
  int write(const char *text, int length) override {
    input.append(text, length); return length;
  }
};
static std::string arrows(const char *direction, int count) {
  std::string result;
  while (count-- > 0) result += direction;
  return result;
}
int main() {
  setlocale(LC_ALL, "C.UTF-8");
  unsetenv("DISPLAY"); unsetenv("WAYLAND_DISPLAY");
  Fl_Term term(0, 0, 640, 320, nullptr, true);
  auto capture = new Capture;
  term.connect(capture, nullptr);
  term.grid(20, 4);
  term.disp("> abcdef");
  assert(term.positionCursor(4, 0));
  assert(capture->input == arrows("\033[D", 4));
  // The shell must echo movement before the screen cursor changes.
  assert(term.snapshot().cursorColumn == 8);
  term.disp("\033[4D"); capture->input.clear();
  assert(term.positionCursor(19, 0));
  assert(capture->input == arrows("\033[C", 4));
  capture->input.clear();
  assert(!term.positionCursor(20, 0) && !term.positionCursor(-1, 0));
  assert(!term.positionCursor(4, 4) && !term.positionCursor(4, -1));
  assert(!term.positionCursor(4, 1)); // blank row
  term.clear(); term.disp("> abc "); term.disp("\033[2D");
  assert(term.positionCursor(19, 0)); // preserve spaces actually typed by the shell
  assert(capture->input == arrows("\033[C", 2)); capture->input.clear();
  assert(!term.positionCursor(4, 1)); // materialized blank rows are not soft wraps
  term.clear(); term.disp("old\r\n> é日éz");
  assert(!term.positionCursor(0, 0)); // old output
  assert(term.positionCursor(4, 1)); // inside wide glyph: use its leading edge
  assert(capture->input == arrows("\033[D", 3));
  capture->input.clear();
  assert(term.positionCursor(6, 1)); // after combining sequence, before z
  assert(capture->input == "\033[D"); capture->input.clear();
  for (auto mode : {"?1049", "?1", "?9", "?1000", "?1001", "?1002", "?1003"}) {
    auto enable = std::string("\033[") + mode + "h";
    auto disable = std::string("\033[") + mode + "l";
    term.disp(enable.c_str());
    assert(!term.positionCursor(2, 1));
    term.disp(disable.c_str());
    assert(term.positionCursor(2, 1));
    capture->input.clear();
  }
  term.disp("\033[?1006;1000h");
  assert(!term.positionCursor(2, 1));
  term.disp("\033[?1000;1006l");
  assert(term.positionCursor(2, 1)); capture->input.clear();
  term.disp("\033[?25l"); assert(!term.positionCursor(2, 1));
  term.disp("\033[?25h");
  term.clear(); term.grid(10, 4);
  term.disp("> 123456789abcdef");
  assert(term.snapshot().cursorRow == 1);
  assert(term.positionCursor(8, 0));
  assert(capture->input == arrows("\033[D", 9));
  capture->input.clear();
  term.disp("\r\nnext\r\nlast\r\nend");
  term.scrollRows(-1);
  assert(!term.positionCursor(0, 3) && capture->input.empty());
  term.clear(); assert(term.positionCursor(0, 0));

  CursorClick click;
  click.begin(1, 10, 10, true); assert(click.finish(1, 11, 10));
  assert(!click.finish(1, 11, 10)); // no paired press
  click.begin(1, 10, 10, true); click.motion(30, 10);
  assert(!click.finish(1, 10, 10)); // dragging back is still a drag
  click.begin(1, 10, 10, false); assert(!click.finish(1, 10, 10));
  click.begin(1, 10, 10, true); assert(!click.finish(2, 10, 10));
  click.begin(1, 10, 10, true); click.cancel();
  assert(!click.finish(1, 10, 10));

  // Exercise a real readline editor, rather than only emitted escape sequences.
  Terminal shell(80, 8, {"/usr/bin/env", "PS1=> ", "HISTFILE=/dev/null",
                         "/bin/bash", "--noprofile", "--norc", "-i"});
  auto wait = [&](auto predicate) {
    for (int i = 0; i < 500; ++i) {
      shell.pty->pump();
      if (predicate()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  };
  assert(wait([&] { return shell.snapshot().cursorColumn == 2; }));
  shell.send("echo ac");
  assert(wait([&] { return shell.snapshot().cursorColumn == 9; }));
  assert(shell.positionCursor(8, 0));
  assert(wait([&] { return shell.snapshot().cursorColumn == 8; }));
  shell.send("b");
  assert(wait([&] { return shell.snapshot().cursorColumn == 9; }));
  shell.send("\r");
  assert(wait([&] { return shell.snapshot().cursorRow == 2; }));
  std::string output;
  for (auto &run : shell.snapshot().runs) if (run.row == 1) output += run.text;
  assert(output == "abc");
  shell.send("echo é日éz");
  assert(wait([&] { return shell.snapshot().cursorColumn == 12; }));
  assert(shell.positionCursor(9, 2)); // middle of 日; move before the glyph
  // Queue insertion after the movement, exactly as an actual typing event does.
  shell.send("X\r");
  assert(wait([&] { return shell.snapshot().cursorRow == 4; }));
  output.clear();
  for (auto &run : shell.snapshot().runs) if (run.row == 3 && !(run.style & terminal_style::padding)) output += run.text;
  assert(output == "éX日éz");
  std::cout << "Cursor click bounds, Unicode, wraps, application modes, gestures and readline passed\n";
}
