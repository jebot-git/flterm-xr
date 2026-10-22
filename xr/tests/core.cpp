// SPDX-License-Identifier: GPL-3.0-or-later
#include "renderer.hpp"
#include <cassert>
#include <chrono>
#include <clocale>
#include <iostream>
#include <thread>
int main() {
  setlocale(LC_ALL, "C.UTF-8");
  unsetenv("DISPLAY");
  unsetenv("WAYLAND_DISPLAY");
  Fl_Term term(0, 0, 640, 320, nullptr, true);
  term.grid(80, 20);
  const std::string input = "hello \033[31mRED\033[0m \033[40mBLACK\033[49m "
                            "DEFAULT\r\nŽluťoučký 日本語\r\n";
  for (auto c : input)
    term.puts(&c, 1);
  auto view = term.snapshot();
  bool red = false, explicitBlack = false, defaultBackground = false,
       unicode = false;
  for (auto &run : view.runs) {
    red |= run.text == "R" && (run.style & 0x20f) == 0x201;
    explicitBlack |= run.text == "B" && (run.style & 0x100);
    defaultBackground |= run.text == "D" && !(run.style & 0x100);
    unicode |= run.text == "日" && run.columns == 2;
  }
  assert(red && explicitBlack && defaultBackground && unicode);
  Renderer renderer;
  renderer.font({Renderer::defaultFont(), 16});
  renderer.render(term, 0, 1);
  assert(renderer.width == 80 * renderer.cw &&
         renderer.height == 21 * renderer.ch);
  bool transparent = false, opaque = false;
  for (size_t i = 3; i < renderer.pixels.size(); i += 4) {
    transparent |= renderer.pixels[i] == 0;
    opaque |= renderer.pixels[i] == 255;
  }
  assert(transparent && opaque);
  auto first = renderer.pixels;
  renderer.light = true;
  renderer.render(term, 0, 1);
  assert(first != renderer.pixels);
  // Software rendering retains UTF-8 box characters even when a PTY read splits
  // their three-byte sequence in an alternate screen.
  Fl_Term alternate(0, 0, 640, 320, nullptr, true);
  alternate.grid(80, 20);
  alternate.disp("\033[?1049h");
  const std::string box = "─";
  for (auto byte : box) alternate.puts(&byte, 1);
  bool boxFound = false;
  for (auto &run : alternate.snapshot().runs) boxFound |= run.text == box;
  assert(boxFound);
  // Widened attributes must survive scrollback reallocation.
  for (int i = 0; i < 5000; ++i)
    term.disp("\033[42mline\033[0m\r\n");
  term.selectAll();
  assert(term.selectionText().find("hello") != std::string::npos);
  auto window = std::make_unique<Terminal>(
      80, 20, std::vector<std::string>{"/bin/sh", "-i"});
  window->send("printf 'RUN_%s\\n' 'λ_日本語'\r");
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
  bool received = false;
  while (std::chrono::steady_clock::now() < end) {
    window->pty->pump();
    window->selectAll();
    if (window->selectionText().find("RUN_λ_日本語") != std::string::npos) {
      received = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(received);
  window->send("exit\r");
  while (window->pty->live() && std::chrono::steady_clock::now() < end) {
    window->pty->pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(!window->pty->live());
  std::cout << "FLTerm parser, UTF-8, colour attributes, raster, scrollback "
               "and PTY passed without a desktop display\n";
}
