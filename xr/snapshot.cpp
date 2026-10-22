// SPDX-License-Identifier: GPL-3.0-or-later
#include "Fl_Term.h"
#include <FL/fl_utf8.h>
#include <algorithm>
#include <cwchar>

static int columns(const char *text, int length) {
  int width = 0;
  for (int i = 0; i < length;) {
    int n = 1;
    unsigned cp = fl_utf8decode(text + i, text + length, &n);
    width += std::max(0, wcwidth(cp));
    i += std::max(1, n);
  }
  return width;
}
void Fl_Term::cellSize(int width, int height) {
  font_width = std::max(1, width);
  font_height = std::max(1, height);
}
void Fl_Term::grid(int cols, int rows) {
  size_x = std::clamp(cols, 10, 240);
  size_y = std::clamp(rows, 2, 100);
  roll_top = 0;
  roll_bot = size_y - 1;
  screen_y = std::max(0, cursor_y - size_y + 1);
  host->send_size(size_x, size_y);
  pending(true);
}
Fl_Term::Snapshot Fl_Term::snapshot() {
  std::lock_guard<std::mutex> lock(append_mtx);
  Snapshot result{{},
                  columns(buff + line[cursor_y], cursor_x - line[cursor_y]),
                  cursor_y - screen_y,
                  bCursor};
  int left = std::min(sel_left, sel_right),
      right = std::max(sel_left, sel_right);
  for (int row = 0; row < size_y && screen_y + row + 1 < line_size; ++row) {
    int begin = line[screen_y + row], end = line[screen_y + row + 1],
        column = 0;
    if (end < begin || end > buff_size)
      continue;
    for (int i = begin; i < end && column < size_x;) {
      int count = 1;
      unsigned cp = fl_utf8decode(buff + i, buff + end, &count);
      count = std::clamp(count, 1, end - i);
      if (cp == '\n' || cp == '\r' || cp < 32) {
        i += count;
        continue;
      }
      int width = std::max(0, wcwidth(cp));
      result.runs.push_back({row, column, width, std::string(buff + i, count),
                             attr[i], i >= left && i < right});
      column += width;
      i += count;
    }
  }
  return result;
}
void Fl_Term::scrollRows(int amount) {
  screen_y =
      std::clamp(screen_y + amount, 0, std::max(0, cursor_y - size_y + 1));
  pending(true);
}
void Fl_Term::pasteText(const std::string &text) {
  if (bBracket)
    write("\033[200~", 6);
  write(text.data(), text.size());
  if (bBracket)
    write("\033[201~", 6);
}
void Fl_Term::selectCells(int column, int row, bool begin) {
  row = std::clamp(screen_y + row, 0, std::max(0, cursor_y));
  int at = line[row], end = line[row + 1], col = 0;
  while (at < end && col < column) {
    int n = 1;
    auto cp = fl_utf8decode(buff + at, buff + end, &n);
    col += std::max(0, wcwidth(cp));
    at += std::max(1, n);
  }
  if (begin)
    sel_left = at;
  sel_right = at;
  pending(true);
}
void Fl_Term::selectAll() {
  sel_left = 0;
  sel_right = std::max(cursor_x, line[cursor_y + 1]);
  pending(true);
}
std::string Fl_Term::selectionText() {
  int first = std::min(sel_left, sel_right),
      last = std::max(sel_left, sel_right);
  return std::string(buff + first, last - first);
}
// Upstream scripting hooks: local commands use the XR frontend's PTY, never a
// network listener or an implicit host connection.
bool show_editor(int, int, int, int) { return false; }
HOST *host_new(const char *) { return new HOST(); }
