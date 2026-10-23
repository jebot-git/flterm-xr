// SPDX-License-Identifier: GPL-3.0-or-later
#include "Fl_Term.h"
#include "terminal-style.hpp"
#include <FL/fl_utf8.h>
#include <cstdlib>
#include <cwchar>

int Fl_Term::cellColumn(int row, int offset) const {
  int column = 0;
  for (int i = line[row]; i < offset;) {
    int n = 1;
    auto cp = fl_utf8decode(buff + i, buff + offset, &n);
    column += std::max(0, wcwidth(cp));
    i += std::max(1, n);
  }
  return column;
}
int Fl_Term::cellOffset(int row, int column) const {
  int i = line[row], col = 0;
  const int end = std::max(i, line[row + 1]);
  while (i < end && buff[i] != '\n' && buff[i] != '\r') {
    int n = 1;
    auto cp = fl_utf8decode(buff + i, buff + end, &n);
    int width = std::max(0, wcwidth(cp));
    if (width && col + width > column)
      break;
    col += width;
    i += std::max(1, n);
  }
  return i;
}
void Fl_Term::replaceRowBytes(int row, int first, int last,
                              std::string_view text, const uint64_t *styles) {
  int end = std::max(cursor_x, line[row + 1]);
  const int finalRow = screen_y + size_y + 1;
  for (int i = row + 1; i <= finalRow; ++i)
    end = std::max(end, line[i]);
  const int delta = int(text.size()) - (last - first);
  reserveBuffer(end + std::max(0, delta) + 1024, finalRow + 4);
  memmove(buff + last + delta, buff + last, end - last);
  memmove(attr + last + delta, attr + last, (end - last) * sizeof(*attr));
  memcpy(buff + first, text.data(), text.size());
  if (styles)
    std::copy(styles, styles + text.size(), attr + first);
  else
    std::fill(attr + first, attr + first + text.size(), c_attr);
  for (int i = row + 1; i <= finalRow; ++i)
    if (i == row + 1 || line[i] != 0)
      line[i] += delta;
  auto adjust = [&](int &value) {
    if (value >= last)
      value += delta;
    else if (value > first)
      value = first;
  };
  adjust(cursor_x);
  // Selection/command anchors at an insertion boundary stay before new text.
  if (sel_left > first)
    adjust(sel_left);
  if (sel_right > first)
    adjust(sel_right);
  if (recv0 > first)
    adjust(recv0);
}
void Fl_Term::replaceCells(int row, int first, int last,
                           std::string_view text) {
  int begin = cellOffset(row, first), end = cellOffset(row, last);
  int left = cellColumn(row, begin), right = cellColumn(row, end);
  // Erasing either half of a wide glyph clears the entire glyph.
  if (right < last && end < line[row + 1] && buff[end] != '\n') {
    int n = 1;
    auto cp = fl_utf8decode(buff + end, buff + line[row + 1], &n);
    right += std::max(0, wcwidth(cp));
    end += std::max(1, n);
  }
  std::string value(std::max(0, first - left), ' ');
  value += text;
  value.append(std::max(0, right - last), ' ');
  replaceRowBytes(row, begin, end, value);
}
void Fl_Term::materializeRows() {
  reserveBuffer(cursor_x + 1024, screen_y + size_y + 4);
  for (int row = screen_y; row < screen_y + size_y; ++row) {
    if (line[row + 1] == 0)
      line[row + 1] = line[row];
    int end = cellOffset(row, size_x);
    int width = cellColumn(row, end);
    if (width < size_x) {
      // Padding must not move a cursor already positioned at end of text.
      int savedRow = cursor_y, savedColumn = cellColumn(cursor_y, cursor_x);
      std::vector<uint64_t> padding(size_x - width, c_attr | terminal_style::padding);
      replaceRowBytes(row, end, end, std::string(size_x - width, ' '), padding.data());
      cursor_x = cellOffset(savedRow, savedColumn);
    }
  }
}
void Fl_Term::putGlyph(std::string_view text) {
  int n = 1;
  auto cp = fl_utf8decode(text.data(), text.data() + text.size(), &n);
  int width = std::max(0, wcwidth(cp));
  int column = cellColumn(cursor_y, cursor_x);
  if (width && column + width > size_x) {
    if (bWraparound) {
      if (column < size_x)
        replaceCells(cursor_y, column, size_x,
                     std::string(size_x - column, ' '));
      if (bAltScreen || line[cursor_y + 2] != 0) {
        vt100_Escape(reinterpret_cast<const unsigned char *>("D"), 1);
        cursor_x = line[cursor_y];
      } else
        next_line();
      column = 0;
    } else {
      column = std::max(0, size_x - width);
      cursor_x = cellOffset(cursor_y, column);
    }
  }
  if (!width) {
    int at = cursor_x;
    replaceRowBytes(cursor_y, at, at, text);
    cursor_x = at + text.size();
    return;
  }
  if (bInsert)
    softwareCsi("1@");
  replaceCells(cursor_y, column, column + width, text);
  cursor_x = cellOffset(cursor_y, column + width);
}
void Fl_Term::scrollRegion(int top, int bottom, int amount) {
  materializeRows();
  int savedRow = cursor_y, savedColumn = cellColumn(cursor_y, cursor_x);
  const int count = std::min(std::abs(amount), bottom - top + 1);
  struct Row {
    std::string text;
    std::vector<uint64_t> styles;
  };
  std::vector<Row> rows;
  for (int r = top; r <= bottom; ++r)
    rows.push_back({std::string(buff + line[r], line[r + 1] - line[r]),
                    std::vector<uint64_t>(attr + line[r], attr + line[r + 1])});
  for (int r = top; r <= bottom; ++r) {
    int source = r - top + (amount > 0 ? count : -count);
    if (source >= 0 && source < int(rows.size())) {
      auto &row = rows[source];
      replaceRowBytes(r, line[r], line[r + 1], row.text, row.styles.data());
    } else
      replaceRowBytes(r, line[r], line[r + 1], std::string(size_x, ' '));
  }
  cursor_x = cellOffset(savedRow, std::min(savedColumn, size_x - 1));
}
bool Fl_Term::softwareCsi(std::string_view sequence) {
  if (sequence.empty())
    return false;
  const char final = sequence.back();
  auto parameters = sequence.substr(0, sequence.size() - 1);
  std::string reply;
  if (sequence == "?2026h") {
    if (!synchronized_snapshot_)
      synchronized_snapshot_ = liveSnapshot();
    synchronized_until_ =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    return true;
  }
  if (sequence == "?2026l") {
    synchronized_snapshot_.reset();
    return true;
  }
  if (keyboard_modes_.command(sequence, bAltScreen, reply)) {
    if (!reply.empty())
      write(reply.data(), reply.size());
    return true;
  }
  if (final == 'm')
    return !parameters.empty() &&
           std::string_view("?<>=").find(parameters.front()) !=
               std::string_view::npos;
  // Private/intermediate CSI sequences must never execute ANSI save/restore.
  if (parameters.find_first_not_of("0123456789;") != parameters.npos) {
    if (final == 'u' || final == 's' || final == 'm')
      return true;
    return false;
  }
  std::vector<int> args(1, 0);
  for (char c : parameters) {
    if (c == ';')
      args.push_back(0);
    else
      args.back() = std::min(100000, args.back() * 10 + c - '0');
  }
  int a = args[0], n = std::max(1, a), column = cellColumn(cursor_y, cursor_x);
  int row = cursor_y - screen_y;
  auto move = [&](int r, int c) {
    materializeRows();
    cursor_y = screen_y + std::clamp(r, bOriginMode ? roll_top : 0,
                                     bOriginMode ? roll_bot : size_y - 1);
    cursor_x = cellOffset(cursor_y, std::clamp(c, 0, size_x - 1));
  };
  switch (final) {
  case 'H':
  case 'f':
    move((a ? a : 1) - 1 + (bOriginMode ? roll_top : 0),
         (args.size() > 1 ? std::max(1, args[1]) : 1) - 1);
    return true;
  case 'G':
  case '`':
    move(row, n - 1);
    return true;
  case 'A':
    move(row - n, column);
    return true;
  case 'B':
  case 'e':
    move(row + n, column);
    return true;
  case 'C':
  case 'a':
    move(row, column + n);
    return true;
  case 'D':
    move(row, column - n);
    return true;
  case 'E':
    move(row + n, 0);
    return true;
  case 'F':
    move(row - n, 0);
    return true;
  case 'd':
    move(n - 1 + (bOriginMode ? roll_top : 0), column);
    return true;
  case 'n':
    if (a == 6)
      reply = "\033[" + std::to_string(row + 1 - (bOriginMode ? roll_top : 0)) +
              ";" + std::to_string(std::min(column, size_x - 1) + 1) + "R";
    else if (a == 5)
      reply = "\033[0n";
    if (!reply.empty())
      write(reply.data(), reply.size());
    return true;
  case 'J':
  case 'K':
  case 'X': {
    if ((final == 'J' && a > 3) || (final == 'K' && a > 2))
      return true;
    materializeRows();
    int firstRow = cursor_y, lastRow = cursor_y;
    if (final == 'J') {
      if (a == 3)
        return true; // Never move the live cursor for ED 3.
      if (a == 1 || a == 2)
        firstRow = screen_y;
      if (a == 0 || a == 2)
        lastRow = screen_y + size_y - 1;
    }
    for (int r = firstRow; r <= lastRow; ++r) {
      int first = 0, last = size_x;
      if (r == cursor_y && a == 0)
        first = column;
      if (r == cursor_y && a == 1)
        last = column + 1;
      if (final == 'X') {
        first = column;
        last = std::min(size_x, column + n);
      }
      replaceCells(r, first, last, std::string(std::max(0, last - first), ' '));
    }
    cursor_x = cellOffset(cursor_y, column);
    return true;
  }
  case 'L':
  case 'M':
    if (row >= roll_top && row <= roll_bot)
      scrollRegion(cursor_y, screen_y + roll_bot, final == 'M' ? n : -n);
    return true;
  case 'S':
  case 'T':
    if (final == 'S' && !bAltScreen && roll_top == 0 && roll_bot == size_y - 1)
      return false;
    scrollRegion(screen_y + roll_top, screen_y + roll_bot,
                 final == 'S' ? n : -n);
    return true;
  case '@':
  case 'P': {
    materializeRows();
    n = std::min(n, size_x - column);
    int begin = cellOffset(cursor_y, column),
        end = cellOffset(cursor_y, size_x);
    if (final == '@') {
      int tail = cellOffset(cursor_y, size_x - n);
      replaceRowBytes(cursor_y, tail, end, "");
      replaceRowBytes(cursor_y, begin, begin, std::string(n, ' '));
    } else {
      int tail = cellOffset(cursor_y, column + n);
      replaceRowBytes(cursor_y, begin, tail, "");
      end = cellOffset(cursor_y, size_x);
      replaceRowBytes(cursor_y, end, end, std::string(n, ' '));
    }
    cursor_x = cellOffset(cursor_y, column);
    return true;
  }
  case 'r': {
    int top = n - 1,
        bottom = args.size() > 1 && args[1] ? args[1] - 1 : size_y - 1;
    if (top >= 0 && top < bottom && bottom < size_y) {
      roll_top = top;
      roll_bot = bottom;
      move(bOriginMode ? top : 0, 0);
    }
    return true;
  }
  case 's':
    save_x = column;
    save_y = row;
    return true;
  case 'u':
    move(save_y, save_x);
    return true;
  default:
    return false;
  }
}
