// SPDX-License-Identifier: GPL-3.0-or-later
#include "terminal-style.hpp"
#include "kitty-diacritics.hpp"
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
  std::lock_guard<std::mutex> lock(append_mtx);
  size_x = std::clamp(cols, 10, 240);
  size_y = std::clamp(rows, 2, 100);
  roll_top = 0;
  roll_bot = size_y - 1;
  if (bAltScreen) {
    main_screen_.margin_top = 0;
    main_screen_.margin_bottom = size_y - 1;
  }
  screen_y = std::max(screen_y, cursor_y - size_y + 1);
  reserveBuffer(cursor_x + 1024, screen_y + size_y + 4);
  trimHistory();
  host->send_size(size_x, size_y);
  pending(true);
}
Fl_Term::Snapshot Fl_Term::snapshot() {
  std::lock_guard<std::mutex> lock(append_mtx);
  if (synchronized_snapshot_ && std::chrono::steady_clock::now()<synchronized_until_)
    return *synchronized_snapshot_;
  synchronized_snapshot_.reset();
  return liveSnapshot();
}
Fl_Term::Snapshot Fl_Term::liveSnapshot() {
  const int top = viewTop();
  Snapshot result{{},
                  columns(buff + line[cursor_y], cursor_x - line[cursor_y]),
                  cursor_y - top,
                  bCursor && view_top_ < 0};
  std::vector<KittyGraphics::Placeholder> placeholders;
  int left = std::min(sel_left, sel_right),
      right = std::max(sel_left, sel_right);
  for (int row = 0; row < size_y && top + row + 1 < line_size; ++row) {
    int begin = line[top + row], end = line[top + row + 1],
        column = 0;
    if (end < begin || end > buff_size)
      continue;
    int previousRow=0,previousColumn=-1,previousHigh=0;
    uint64_t previousColor=~uint64_t(0);
    for (int i = begin; i < end && column < size_x;) {
      int count = 1;
      unsigned cp = fl_utf8decode(buff + i, buff + end, &count);
      count = std::clamp(count, 1, end - i);
      if (cp == '\n' || cp == '\r' || cp < 32) {
        i += count;
        continue;
      }
      int width = std::max(0, wcwidth(cp));
      if(cp==0x10eeee) {
        int marks[3]={-1,-1,-1},at=i+count,nmarks=0;
        while(at<end && nmarks<3) {
          int n=1;auto mark=fl_utf8decode(buff+at,buff+end,&n);
          auto found=std::find(std::begin(kitty_diacritics),std::end(kitty_diacritics),mark);
          if(found==std::end(kitty_diacritics))break;
          marks[nmarks++]=found-std::begin(kitty_diacritics);at+=std::max(1,n);
        }
        auto colors=image_colors_[i];int imageRow=std::max(0,marks[0]),imageColumn=std::max(0,marks[1]),high=std::max(0,marks[2]);
        bool inherit=previousColor==colors;
        if(!nmarks && inherit)imageRow=previousRow;
        if(nmarks<2 && inherit && imageRow==previousRow)imageColumn=previousColumn+1;
        if(nmarks<3 && inherit && imageRow==previousRow && imageColumn==previousColumn+1)high=previousHigh;
        placeholders.push_back({uint32_t(colors)|(uint32_t(high&255)<<24),uint32_t(colors>>32),column,row,imageColumn,imageRow});
        previousColor=colors;previousRow=imageRow;previousColumn=imageColumn;previousHigh=high;
        // Draw the background, but neither the private-use glyph nor its marks.
        result.runs.push_back({row,column,1," ",attr[i],i>=left&&i<right,link_uris_[link_ids_[i]]});
        column+=1;i=at;continue;
      }
      previousColor=~uint64_t(0);
      result.runs.push_back({row, column, width, std::string(buff + i, count),
                             attr[i], i >= left && i < right, link_uris_[link_ids_[i]]});
      column += width;
      i += count;
    }
  }
  result.images=graphics_[bAltScreen].snapshot(top,size_y,font_width,font_height,placeholders);
  return result;
}
void Fl_Term::scrollRows(int amount) {
  std::lock_guard<std::mutex> lock(append_mtx);
  if (bAltScreen)
    return;
  const auto top = std::clamp<int64_t>(int64_t(viewTop()) + amount, 0, screen_y);
  view_top_ = top == screen_y ? -1 : int(top);
  pending(true);
}
void Fl_Term::scrollToBottom() {
  std::lock_guard<std::mutex> lock(append_mtx);
  view_top_ = -1;
  pending(true);
}
void Fl_Term::pasteText(const std::string &text) {
  scrollToBottom();
  if (bBracket)
    write("\033[200~", 6);
  write(text.data(), text.size());
  if (bBracket)
    write("\033[201~", 6);
}
void Fl_Term::selectCells(int column, int row, bool begin) {
  std::lock_guard<std::mutex> lock(append_mtx);
  row = std::clamp(viewTop() + row, 0, screen_y + size_y - 1);
  int at = line[row], end = line[row + 1], col = 0;
  if (row > 0 && at == 0) {
    int previous = row;
    while (previous > 0 && line[previous] == 0) --previous;
    at = end = line[previous];
  }
  end = std::max(at, end);
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
  std::lock_guard<std::mutex> lock(append_mtx);
  sel_left = 0;
  sel_right = cursor_x;
  for (int row = 0; row <= screen_y + size_y && row < line_size; ++row)
    sel_right = std::max(sel_right, line[row]);
  pending(true);
}
std::string Fl_Term::selectionText() {
  std::lock_guard<std::mutex> lock(append_mtx);
  int first = std::min(sel_left, sel_right),
      last = std::max(sel_left, sel_right);
  return std::string(buff + first, last - first);
}
// Upstream scripting hooks: local commands use the XR frontend's PTY, never a
// network listener or an implicit host connection.
bool show_editor(int, int, int, int) { return false; }
HOST *host_new(const char *) { return new HOST(); }

// Translate a click to shell editing keys; changing only the painted cursor
// would leave the PTY's line editor at its old insertion point.
bool Fl_Term::positionCursor(int column, int row) {
  std::string keys;
  {
    std::lock_guard<std::mutex> lock(append_mtx);
    if (view_top_ >= 0 || bAltScreen || bAppCursor || mouseTracking_ || !bCursor ||
        column < 0 || column >= size_x || row < 0 || row >= size_y)
      return false;
    row += screen_y;
    // Only the current logical line can be edited. Soft wraps have no LF;
    // never send Up/Down, which would traverse shell history.
    int first = cursor_y, last = cursor_y;
    while (first > screen_y && line[first] > line[first - 1] &&
           buff[line[first] - 1] != '\n' &&
           !(attr[line[first] - 1] & terminal_style::padding)) --first;
    while (last + 1 < screen_y + size_y && line[last + 1] > line[last] &&
           buff[line[last + 1] - 1] != '\n' &&
           !(attr[line[last + 1] - 1] & terminal_style::padding) &&
           line[last + 2] > line[last + 1]) ++last;
    if (row < first || row > last) return false;
    int end = std::max(line[row], line[row + 1]);
    while (end > line[row] && (attr[end - 1] & terminal_style::padding)) --end;
    int target = line[row], col = 0;
    while (target < end) {
      int n = 1;
      unsigned cp = fl_utf8decode(buff + target, buff + end, &n);
      int width = std::max(0, wcwidth(cp));

      if (cp < 32 || (width > 0 && col + width > column)) break;
      target += std::clamp(n, 1, end - target);
      col += width;
    }
    int distance = 0;
    const int stop = std::max(target, cursor_x);
    for (int i = std::min(target, cursor_x); i < stop;) {
      int n = 1;
      unsigned cp = fl_utf8decode(buff + i, buff + stop, &n);
      if (wcwidth(cp) > 0) ++distance;
      i += std::clamp(n, 1, stop - i);
    }
    for (int i = 0; i < distance; ++i)
      keys += target < cursor_x ? "\033[D" : "\033[C";
  }
  if (!keys.empty()) write(keys.data(), keys.size());
  return true;
}
