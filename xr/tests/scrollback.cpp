// SPDX-License-Identifier: GPL-3.0-or-later
#include "renderer.hpp"
#include <cassert>
#include <climits>
#include <clocale>
#include <iostream>
#include <string>
#include <vector>

static std::vector<std::string> rows(Fl_Term &term) {
  std::vector<std::string> result(term.sizeY());
  for (auto &run : term.snapshot().runs) result[run.row] += run.text;
  for (auto &row : result)
    while (!row.empty() && row.back() == ' ') row.pop_back();
  return result;
}
static void numbered(Fl_Term &term, int first, int last, bool wide = false) {
  std::string chunk;
  for (int i = first; i < last; ++i) {
    char label[32]; snprintf(label, sizeof(label), "L%05d", i);
    chunk += "\033[32m" + std::string(label);
    if (wide) for (int n = 0; n < 100; ++n) chunk += "é";
    chunk += "\033[0m\r\n";
    if (i % 128 == 127) { term.disp(chunk.c_str()); chunk.clear(); }
  }
  term.disp(chunk.c_str());
}
int main() {
  setlocale(LC_ALL, "C.UTF-8");
  unsetenv("DISPLAY"); unsetenv("WAYLAND_DISPLAY");
  Fl_Term term(0, 0, 640, 320, nullptr, true);
  term.grid(120, 4);
  term.disp("start");
  term.selectCells(0, 0, true); term.selectCells(0, 3, false);
  assert(term.selectionText() == "start");
  term.clear();
  numbered(term, 0, 12);
  assert(term.historyRows() == 9);
  assert(rows(term)[0] == "L00009");
  term.scrollRows(-3); // Standard wheel up reveals earlier rows.
  assert(term.scrollOffset() == 3 && rows(term)[0] == "L00006");
  assert(!term.snapshot().cursor);
  term.selectCells(0, 0, true); term.selectCells(6, 0, false);
  assert(term.selectionText() == "L00006");
  auto held = rows(term);
  numbered(term, 12, 14);
  assert(rows(term) == held && term.scrollOffset() == 5);
  term.disp("\033[4;1HLIVE"); // Output addresses the live screen, not the view.
  assert(rows(term) == held);
  term.grid(120, 3);
  assert(rows(term)[0] == held[0]);
  term.scrollRows(INT_MAX);
  assert(term.scrollOffset() == 0 && term.snapshot().cursor);
  assert(rows(term)[2] == "LIVE");
  term.scrollRows(INT_MIN);
  assert(rows(term)[0] == "L00000");
  term.scrollRows(1);
  assert(rows(term)[0] == "L00001");
  // Alternate screens neither expose nor overwrite the normal history/view.
  const auto main = rows(term);
  const int offset = term.scrollOffset();
  term.disp("\033[?1049h\033[?1049h\033[HEDITOR");
  assert(term.alternateScreen() && term.historyRows() == 0);
  term.scrollRows(INT_MIN);
  assert(term.scrollOffset() == 0 && rows(term)[0] == "EDITOR");
  numbered(term, 0, 50);
  assert(term.historyRows() == 0);
  term.disp("\033[?1049l\033[?1049l");
  assert(!term.alternateScreen());
  assert(rows(term) == main && term.scrollOffset() == offset);
  // Paste returns to the live screen even without a host attached.
  term.pasteText("");
  assert(term.scrollOffset() == 0);
  // Full-screen IND and CSI S retain displaced rows in history.
  Fl_Term indexed(0, 0, 640, 320, nullptr, true);
  indexed.grid(20, 4);
  indexed.disp("\033[2J\033[1;1Hfirst\033[2;1Hsecond\033[3;1Hthird\033[4;1Hfourth");
  indexed.disp("\r\n");
  assert(indexed.historyRows() == 1 && rows(indexed)[0] == "second");
  indexed.disp("\033[S");
  assert(indexed.historyRows() == 2 && rows(indexed)[0] == "third");
  indexed.scrollRows(INT_MIN);
  assert(rows(indexed)[0] == "first" && rows(indexed)[1] == "second");
  // A partial scroll region must leave history and outside rows alone.
  indexed.scrollToBottom();
  indexed.disp("\033[2;3r\033[3;1H\n");
  assert(indexed.historyRows() == 2 && rows(indexed)[0] == "third");
  // Resizing an alternate screen also updates the restored normal margins.
  Fl_Term resized(0, 0, 640, 320, nullptr, true);
  resized.grid(20, 4);
  numbered(resized, 0, 8);
  resized.disp("\033[?1049h"); resized.grid(20, 6);
  resized.disp("\033[?1049l\033[6;1H\033D");
  assert(!resized.alternateScreen() && resized.historyRows() == 6);
  resized.scrollRows(INT_MIN);
  assert(rows(resized)[0] == "L00000");
  // Cross both row and byte allocation boundaries, then evict oldest rows.
  term.clear(); term.grid(120, 4);
  numbered(term, 0, 9900, true);
  term.scrollRows(-3);
  term.selectCells(0, 0, true); term.selectCells(6, 0, false);
  const auto selected = term.selectionText();
  const auto pinned = rows(term);
  numbered(term, 9900, 10500, true);
  assert(term.historyRows() == 10000);
  assert(rows(term) == pinned && term.selectionText() == selected);
  term.scrollRows(INT_MIN);
  assert(rows(term)[0].starts_with("L00497é"));
  term.selectCells(0, 0, true); term.selectCells(6, 0, false);
  numbered(term, 10500, 10510, true);
  assert(term.scrollOffset() == 10000 && rows(term)[0].starts_with("L00507é"));
  assert(term.selectionText().empty());
  bool unicode = false, green = false;
  for (auto &run : term.snapshot().runs) {
    unicode |= run.text == "é";
    green |= run.text == "L" && (run.style & 0x20f) == 0x202;
  }
  assert(unicode && green);
  term.selectAll();
  assert(term.selectionText().starts_with("L00507"));
  assert(term.selectionText().find("L00000") == std::string::npos);
  // No scrollbar, gutter or lost terminal columns in the raster path.
  Renderer renderer; renderer.font({Renderer::defaultFont(), 16});
  renderer.render(term, 0, 1);
  assert(renderer.width == 120 * renderer.cw);
  // Clear selection so the unused rightmost cells have the usual background.
  term.selectCells(0, 0, true);
  renderer.render(term, 0, 1);
  for (int y = 0; y < term.sizeY() * renderer.ch; ++y)
    for (int x = renderer.width - 8; x < renderer.width; ++x)
      assert(renderer.pixels[(y * renderer.width + x) * 4 + 3] == 0);
  term.scrollToBottom();
  term.scrollRows(INT_MIN); term.clear();
  assert(term.historyRows() == 0 && term.scrollOffset() == 0);
  std::cout << "Scrollback bounds, direction, output anchoring, selection, UTF-8/styles, "
               "resize, alternate/partial screens and scrollbar-free rendering passed\n";
}
