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
  memmove(image_colors_.data()+last+delta,image_colors_.data()+last,(end-last)*sizeof(uint64_t));
  std::fill(image_colors_.begin()+first,image_colors_.begin()+first+text.size(),0);
  memmove(link_ids_.data()+last+delta, link_ids_.data()+last, (end-last)*sizeof(uint32_t));
  std::fill(link_ids_.begin()+first, link_ids_.begin()+first+text.size(), 0);
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
    std::fill(image_colors_.begin()+at,image_colors_.begin()+at+text.size(),image_color_);
    std::fill(link_ids_.begin()+at, link_ids_.begin()+at+text.size(), active_link_);
    cursor_x = at + text.size();
    return;
  }
  if (bInsert)
    softwareCsi("1@");
  replaceCells(cursor_y, column, column + width, text);
  cursor_x = cellOffset(cursor_y, column + width);
  std::fill(image_colors_.begin()+cellOffset(cursor_y,column),image_colors_.begin()+cursor_x,image_color_);
  std::fill(link_ids_.begin()+cellOffset(cursor_y,column), link_ids_.begin()+cursor_x, active_link_);
}
void Fl_Term::scrollRegion(int top, int bottom, int amount) {
  graphics_[bAltScreen].scroll(top,bottom,amount,font_width,font_height);
  materializeRows();
  int savedRow = cursor_y, savedColumn = cellColumn(cursor_y, cursor_x);
  const int count = std::min(std::abs(amount), bottom - top + 1);
  struct Row {
    std::string text;
    std::vector<uint64_t> styles;
    std::vector<uint32_t> links;
    std::vector<uint64_t> colors;
  };
  std::vector<Row> rows;
  for (int r = top; r <= bottom; ++r)
    rows.push_back({std::string(buff + line[r], line[r + 1] - line[r]),
                    std::vector<uint64_t>(attr + line[r], attr + line[r + 1]),
                    std::vector<uint32_t>(link_ids_.begin()+line[r],link_ids_.begin()+line[r+1]),
                    std::vector<uint64_t>(image_colors_.begin()+line[r],image_colors_.begin()+line[r+1])});
  for (int r = top; r <= bottom; ++r) {
    int source = r - top + (amount > 0 ? count : -count);
    if (source >= 0 && source < int(rows.size())) {
      auto &row = rows[source];
      replaceRowBytes(r, line[r], line[r + 1], row.text, row.styles.data());
      std::copy(row.colors.begin(),row.colors.end(),image_colors_.begin()+line[r]);
      std::copy(row.links.begin(),row.links.end(),link_ids_.begin()+line[r]);
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
  if (final == 'm') {
    if (parameters.empty() || std::string_view("?<>=").find(parameters.front()) == std::string_view::npos) imageColors(parameters);
    return !parameters.empty() &&
           std::string_view("?<>=").find(parameters.front()) !=
               std::string_view::npos;
  }
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
  case 't':
    if(a==14 || a==16 || a==18) {
      reply="\033["+std::to_string(a==14?4:a==16?6:8)+";"+
        std::to_string(a==14?size_y*font_height:a==16?font_height:size_y)+";"+
        std::to_string(a==14?int(size_x*font_width):a==16?int(font_width):size_x)+"t";
      write(reply.data(),reply.size());
    }
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
    if (final=='J' && a==2) graphics_[bAltScreen].clearVisible(screen_y,size_y,font_width,font_height);
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

void Fl_Term::graphicsCommand(std::string_view payload) {
  auto result=graphics_[bAltScreen].command(payload,{cellColumn(cursor_y,cursor_x),cursor_y,screen_y,size_y,size_x,int(font_width),font_height});
  if(!result.reply.empty())write(result.reply.data(),result.reply.size());
  if(result.columns || result.rows) {
    int column=std::min(size_x-1,cellColumn(cursor_y,cursor_x)+result.columns);
    for(int row=0;row<std::min(result.rows,size_y);++row)
      vt100_Escape(reinterpret_cast<const unsigned char *>("D"),1);
    materializeRows();cursor_x=cellOffset(cursor_y,column);
  }
}
bool Fl_Term::animateGraphics() {
  std::lock_guard<std::mutex> lock(append_mtx);
  bool changed=graphics_[bAltScreen].animate();if(changed)pending(true);return changed;
}
// Preserve the numeric palette index (not its RGB expansion) for Kitty image
// IDs, and the independent underline color used as a placement ID.
void Fl_Term::imageColors(std::string_view sequence) {
  std::vector<std::vector<int>> groups(1,std::vector<int>(1,-1));
  for(char c:sequence) {
    if(c==';')groups.push_back({-1});
    else if(c==':')groups.back().push_back(-1);
    else if(c>='0'&&c<='9') {auto &v=groups.back().back();v=std::min(100000,v<0?c-'0':v*10+c-'0');}
    else return;
  }
  auto set=[&](bool underline,uint32_t value) {
    if(underline)image_color_=(image_color_&0xffffffffu)|(uint64_t(value)<<32);
    else image_color_=(image_color_&0xffffffff00000000ull)|value;
  };
  for(size_t i=0;i<groups.size();++i) {
    int code=std::max(0,groups[i][0]);
    if(!code)image_color_=0;
    else if(code==39)set(false,0);
    else if(code==59)set(true,0);
    else if(code>=30 && code<=37)set(false,code-30);
    else if(code>=90 && code<=97)set(false,code-90+8);
    else if(code==38 || code==48 || code==58) {
      auto args=groups[i];
      if(args.size()==1 && i+1<groups.size()) {
        int n=groups[i+1][0]==5?2:groups[i+1][0]==2?4:1;
        while(n-- && i+1<groups.size())args.push_back(groups[++i][0]);
      }
      if(code==48)continue;
      if(args.size()==3 && args[1]==5 && args[2]>=0 && args[2]<=255)set(code==58,args[2]);
      else if((args.size()==5 || args.size()==6) && args[1]==2) {
        size_t at=args.size()==6?3:2;
        if(args[at]>=0 && args[at]<=255 && args[at+1]>=0 && args[at+1]<=255 && args[at+2]>=0 && args[at+2]<=255)
          set(code==58,(args[at]<<16)|(args[at+1]<<8)|args[at+2]);
      }
    }
  }
}
