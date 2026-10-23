// SPDX-License-Identifier: GPL-3.0-or-later
#include "renderer.hpp"
#include <cassert>
#include <clocale>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

struct CaptureHost : HOST {
  std::string replies;
  int live() override { return true; }
  int write(const char *data, int size) override {
    replies.append(data, size);
    return size;
  }
  void connect() override {}
};
static std::vector<std::string> rows(Fl_Term &term) {
  std::vector<std::string> result(term.sizeY());
  for (const auto &run : term.snapshot().runs) {
    assert(run.column >= 0 && run.column + run.columns <= term.sizeX());
    result[run.row] += run.text;
  }
  for (auto &row : result)
    while (!row.empty() && row.back() == ' ')
      row.pop_back();
  return result;
}
static void fragmented(Fl_Term &term, std::string_view text, size_t chunk) {
  for (size_t i = 0; i < text.size(); i += chunk)
    term.puts(text.data() + i, std::min(chunk, text.size() - i));
}
int main(int argc, char **argv) {
  setlocale(LC_ALL, "C.UTF-8");
  unsetenv("DISPLAY");
  unsetenv("WAYLAND_DISPLAY");
  for (size_t chunk : {1, 2, 7, 4096}) {
    Fl_Term term(0, 0, 800, 480, nullptr, true);
    term.grid(20, 6);
    term.callback([](Fl_Widget *, void *) {});
    auto host = new CaptureHost;
    term.connect(host, nullptr);
    fragmented(term,
               "\033[?1049h\033[H╭──────────────────╮\033[2;1H│› Žluťoučký "
               "日本語│\033[3;1Hbelow\033[6;1Hbottom",
               chunk);
    auto initial = rows(term);
    assert(initial[0] == "╭──────────────────╮" && initial[2] == "below" &&
           initial[5] == "bottom");
    fragmented(term, "\033[2;4Hé\033[2;7HŽ\033[3;1Hunchanged", chunk);
    assert(rows(term)[0] == initial[0] && rows(term)[5] == "bottom");
    assert(rows(term)[2] == "unchanged");
    // Redraw after replacing multi-byte characters, including a wide cell.
    fragmented(term, "\033[2;1H\033[2KASCII\033[3;1H日本語\033[K\033[3;3HX",
               chunk);
    assert(rows(term)[1] == "ASCII" && rows(term)[2] == "日X 語");
    fragmented(term, "\033[4;5H\033[2JX", chunk);
    auto cleared = rows(term);
    assert(cleared[3] == "    X");
    for (int i : {0, 1, 2, 4, 5})
      assert(cleared[i].empty());
    assert(term.snapshot().cursorRow == 3 && term.snapshot().cursorColumn == 5);
    fragmented(term, "\033[1GX\033[5n\033[6n", chunk);
    assert(rows(term)[3] == "X   X");
    assert(host->replies == "\033[0n\033[4;2R");
    host->replies.clear();
    fragmented(term, "\033]10;?\033\\\033]11;?\033\\", chunk);
    assert(host->replies ==
           "\033]10;rgb:e7e7/ebeb/f1f1\033\\\033]11;rgb:1616/1c1c/2323\033\\");
    host->replies.clear();
    const auto completed = rows(term);
    fragmented(term, "\033[?2026h\033[1;1HPARTIAL", chunk);
    assert(rows(term) == completed);
    fragmented(term, "\033[?2026l", chunk);
    assert(rows(term)[0] == "PARTIAL");
    term.disp("\033[4;2H");
    // Codex negotiates Kitty before and after entering its alternate screen.
    fragmented(term,
               "\033[s\033[5;7H\033[?u\033[>7u\033[?u\033[=24;2u\033[?u\033[=2;"
               "3u\033[?u",
               chunk);
    assert(host->replies == "\033[?0u\033[?7u\033[?31u\033[?29u");
    assert(term.snapshot().cursorRow == 4 && term.snapshot().cursorColumn == 6);
    fragmented(term, "\033[<u\033[?u\033[u", chunk);
    assert(term.keyboardFlags() == 0 && term.snapshot().cursorRow == 3 &&
           term.snapshot().cursorColumn == 1);
    fragmented(term, "\033[?1049l\033[>3u\033[?1049h\033[>31u\033[?1049l",
               chunk);
    assert(term.keyboardFlags() == 3);
    // Editing and scrolling must copy full UTF-8 rows and their styles.
    fragmented(term,
               "\033[?1049h\033[2J\033[Hαβγ\033[2;1H日本\033[3;1Hthree\033[4;"
               "1Hfour\033[5;1Hfive\033[6;1Hsix\033[2;5r\033[5;1H\n",
               chunk);
    auto scrolled = rows(term);
    assert(scrolled[0] == "αβγ" && scrolled[1] == "three" &&
           scrolled[2] == "four" && scrolled[3] == "five" &&
           scrolled[4].empty() && scrolled[5] == "six");
    fragmented(term, "\033[2;1H\033[L", chunk);
    assert(rows(term)[1].empty() && rows(term)[2] == "three" &&
           rows(term)[5] == "six");
    fragmented(term, "\033[r\033[2J\033[H\033(0lqqk\033(B", chunk);
    assert(rows(term)[0] == "┌──┐");
    fragmented(term, "\033[H" + std::string(19, 'x') + "日", chunk);
    assert(rows(term)[0] == std::string(19, 'x') && rows(term)[1] == "日");
  }
  if (argc > 1) {
    std::ifstream input(argv[1], std::ios::binary);
    assert(input);
    std::string capture((std::istreambuf_iterator<char>(input)), {});
    Fl_Term term(0, 0, 800, 480, nullptr, true);
    term.grid(100, 30);
    term.callback([](Fl_Widget *, void *) {});
    fragmented(term, capture, 1);
    for (const auto &row : rows(term))
      std::cout << row << '\n';
    if (argc > 2) {
      Renderer renderer;
      renderer.font({Renderer::defaultFont(), 16});
      renderer.opacity = 1;
      renderer.render(term, 0, 1);
      std::ofstream output(argv[2], std::ios::binary);
      output << "P6\n" << renderer.width << ' ' << renderer.height << "\n255\n";
      for (int y = 0; y < renderer.height; ++y)
        for (int x = 0; x < renderer.width; ++x) {
          auto at = (size_t(renderer.height - 1 - y) * renderer.width + x) * 4;
          output.write(
              reinterpret_cast<const char *>(renderer.pixels.data() + at), 3);
        }
    }
  }
  std::cout << "PASS: Codex UTF-8 redraws, cell addressing/erase, scrolling, "
               "DSR and independent Kitty mode stacks\n";
}
