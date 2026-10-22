// SPDX-License-Identifier: GPL-3.0-or-later
#include "renderer.hpp"
#include "bdf_font.hpp"
#include <FL/fl_utf8.h>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include FT_FONT_FORMATS_H
Terminal::Terminal(int cols, int rows, const std::vector<std::string> &command)
    : Fl_Term(0, 0, cols * 8, rows * 16, nullptr, true),
      pty(new PtyHost(command)) {
  callback([](Fl_Widget *, void *) {});
  connect(pty, nullptr);
  grid(cols, rows);
}
std::string Renderer::defaultFont() {
  if (std::filesystem::exists(FLTERM_FONT_DIR "/unifont-17.0.04.bdf"))
    return FLTERM_FONT_DIR "/unifont-17.0.04.bdf";
  return FLTERM_BUILD_FONT;
}
Renderer::Renderer() {
  if (FT_Init_FreeType(&ft))
    throw std::runtime_error("Cannot initialize FreeType");
}
Renderer::~Renderer() {
  for (auto &[_, value] : fallback)
    FT_Done_Face(value);
  if (face)
    FT_Done_Face(face);
  if (ft)
    FT_Done_FreeType(ft);
}
void Renderer::font(const FontChoice &next) {
  if (face && next == choice)
    return;
  FT_Face prepared{};
  if (FT_New_Face(ft, next.path.c_str(), 0, &prepared))
    throw std::runtime_error("Cannot open BDF: " + next.path);
  if (std::string(FT_Get_Font_Format(prepared)) != "BDF" ||
      !prepared->num_fixed_sizes) {
    FT_Done_Face(prepared);
    throw std::runtime_error(
        "DomeD terminals require a native BDF bitmap font");
  }
  int strike = 0;
  for (int i = 1; i < prepared->num_fixed_sizes; ++i)
    if (abs(prepared->available_sizes[i].height - next.pixels) <
        abs(prepared->available_sizes[strike].height - next.pixels))
      strike = i;
  FT_Select_Size(prepared, strike);
  if (FT_Load_Char(prepared, 'M', FT_LOAD_RENDER | FT_LOAD_TARGET_MONO)) {
    FT_Done_Face(prepared);
    throw std::runtime_error("BDF has no terminal cells");
  }
  int newWidth = std::max(1, int(prepared->glyph->advance.x / 64));
  int newHeight = std::max(1, int(prepared->size->metrics.height / 64));
  for (auto &[_, value] : fallback)
    FT_Done_Face(value);
  fallback.clear();
  glyphFaces.clear();
  if (face)
    FT_Done_Face(face);
  face = prepared;
  choice = next;
  cw = newWidth;
  ch = newHeight;
  baseline = std::clamp(int(face->size->metrics.ascender / 64), 1, ch);
}
FT_Face Renderer::glyphFace(unsigned cp) {
  if (FT_Get_Char_Index(face, cp))
    return face;
  if (auto it = glyphFaces.find(cp); it != glyphFaces.end())
    return it->second;
  for (const auto &item : bdf_fonts::discover()) {
    if (abs(item.pixels - choice.pixels) > choice.pixels / 2)
      continue;
    FT_Face value{};
    if (auto it = fallback.find(item.file); it != fallback.end())
      value = it->second;
    else {
      if (FT_New_Face(ft, item.file.c_str(), 0, &value))
        continue;
      if (!value->num_fixed_sizes) {
        FT_Done_Face(value);
        continue;
      }
      FT_Select_Size(value, 0);
      fallback[item.file] = value;
    }
    if (FT_Get_Char_Index(value, cp))
      return glyphFaces[cp] = value;
  }
  return glyphFaces[cp] = face;
}
std::array<uint8_t, 3> Renderer::foreground() const {
  return light ? std::array<uint8_t, 3>{30, 37, 46}
               : std::array<uint8_t, 3>{231, 235, 241};
}
std::array<uint8_t, 3> Renderer::background() const {
  return light ? std::array<uint8_t, 3>{255, 255, 255}
               : std::array<uint8_t, 3>{22, 28, 35};
}
void Renderer::rect(int x, int y, int w, int h, std::array<uint8_t, 3> color,
                    float alpha) {
  uint8_t a = std::clamp(int(alpha * 255 + .5f), 0, 255);
  for (int yy = std::max(0, y); yy < std::min(height, y + h); ++yy)
    for (int xx = std::max(0, x); xx < std::min(width, x + w); ++xx) {
      size_t at = (size_t(height - 1 - yy) * width + xx) * 4;
      for (int c = 0; c < 3; ++c)
        pixels[at + c] = color[c] * a / 255;
      pixels[at + 3] = a;
    }
}
void Renderer::glyph(unsigned cp, int x, int y, int cols,
                     std::array<uint8_t, 3> color) {
  auto selected = glyphFace(cp);
  if (FT_Load_Char(selected, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO))
    return;
  auto slot = selected->glyph;
  auto &bitmap = slot->bitmap;
  for (unsigned yy = 0; yy < bitmap.rows; ++yy)
    for (unsigned xx = 0; xx < bitmap.width; ++xx) {
      int dx = x + slot->bitmap_left + xx,
          dy = y + baseline - slot->bitmap_top + yy;
      if (dx < x || dx >= x + std::max(1, cols) * cw || dy < y ||
          dy >= y + ch || dx < 0 || dy < 0 || dx >= width || dy >= height)
        continue;
      auto row =
          bitmap.buffer +
          (bitmap.pitch >= 0 ? yy : bitmap.rows - 1 - yy) * abs(bitmap.pitch);
      unsigned a = bitmap.pixel_mode == FT_PIXEL_MODE_MONO
                       ? (row[xx / 8] & (128 >> (xx % 8)) ? 255 : 0)
                       : row[xx];
      if (!a)
        continue;
      size_t at = (size_t(height - 1 - dy) * width + dx) * 4;
      for (int c = 0; c < 3; ++c)
        pixels[at + c] = (color[c] * a + pixels[at + c] * (255 - a)) / 255;
      pixels[at + 3] = a + pixels[at + 3] * (255 - a) / 255;
    }
}
void Renderer::render(Fl_Term &term, int tab, int count) {
  width = term.sizeX() * cw;
  height = (term.sizeY() + 1) * ch;
  pixels.assign(size_t(width) * height * 4, 0);
  rect(0, 0, width, height, background(), opacity);
  static const std::array<uint8_t, 3> ansi[16] = {
      {0, 0, 0},       {192, 0, 0},    {0, 192, 0},    {192, 192, 0},
      {32, 96, 192},   {192, 0, 192},  {0, 192, 192},  {192, 192, 192},
      {80, 80, 80},    {255, 80, 80},  {80, 255, 80},  {255, 255, 80},
      {100, 140, 255}, {255, 80, 255}, {80, 255, 255}, {255, 255, 255}};
  auto snapshot = term.snapshot();
  for (const auto &run : snapshot.runs) {
    auto fg = run.style & 0x200 ? ansi[run.style & 15] : foreground();
    if (run.selected) {
      rect(run.column * cw, run.row * ch,
           width - run.column * cw < run.columns * cw ? width - run.column * cw
                                                      : run.columns * cw,
           ch, accent, 1);
      fg = background();
    } else if (run.style & 0x100)
      rect(run.column * cw, run.row * ch, run.columns * cw, ch,
           ansi[(run.style >> 4) & 15], styledOpacity);
    int n = 1;
    unsigned cp =
        fl_utf8decode(run.text.data(), run.text.data() + run.text.size(), &n);
    glyph(cp, run.column * cw, run.row * ch, run.columns, fg);
  }
  if (snapshot.cursor && snapshot.cursorRow >= 0 &&
      snapshot.cursorRow < term.sizeY())
    rect(snapshot.cursorColumn * cw, snapshot.cursorRow * ch + ch - 2, cw, 2,
         foreground(), 1);
  int column = snapshot.cursorColumn;
  for (size_t i = 0; i < preedit.size();) {
    int n = 1;
    unsigned cp =
        fl_utf8decode(preedit.data() + i, preedit.data() + preedit.size(), &n);
    int cells = std::max(1, wcwidth(cp));
    glyph(cp, column * cw, snapshot.cursorRow * ch, cells, accent);
    rect(column * cw, (snapshot.cursorRow + 1) * ch - 1, cells * cw, 1, accent,
         1);
    column += cells;
    i += std::max(1, n);
  }
  std::string label = " " + std::to_string(tab + 1) + "/" +
                      std::to_string(count) + " FLTerm XR";
  rect(0, term.sizeY() * ch, width, ch, background(),
       opacity > 0 ? std::max(.3f, opacity) : 0);
  for (size_t i = 0; i < label.size() && i < size_t(term.sizeX()); ++i)
    glyph(label[i], i * cw, term.sizeY() * ch, 1, accent);
  term.pending(false);
}
