// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Fl_Term.h"
#include "dome-font.hpp"
#include "pty.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <array>
#include <map>
#include <memory>
struct Terminal : Fl_Term {
  PtyHost *pty;
  explicit Terminal(int cols, int rows,
                    const std::vector<std::string> &command, std::string realm = "DE");
  void cellSize(int width,int height) { Fl_Term::cellSize(width,height);pty->cellSize(width,height); }
  void draw() override {}
};
struct Renderer {
  FT_Library ft{};
  FT_Face face{};
  FontChoice choice;
  int cw = 8, ch = 16, baseline = 14, width = 0, height = 0;
  bool light = false;
  float opacity = 0, styledOpacity = 1;
  std::string preedit;
  std::array<uint8_t, 3> accent{107, 171, 255};
  std::vector<uint8_t> pixels;
  std::map<std::string, FT_Face> fallback;
  std::map<unsigned, FT_Face> glyphFaces;
  Renderer();
  ~Renderer();
  void font(const FontChoice &);
  FT_Face glyphFace(unsigned);
  void render(Fl_Term &, int tab, int count);
  void rect(int, int, int, int, std::array<uint8_t, 3>, float);
  void glyph(unsigned, int, int, int, std::array<uint8_t, 3>);
  std::array<uint8_t, 3> foreground() const;
  std::array<uint8_t, 3> background() const;
  static std::string defaultFont();
};
