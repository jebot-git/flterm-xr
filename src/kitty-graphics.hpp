// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Per-screen protocol state. Coordinates are absolute history rows; rendering
// converts them to viewport pixels, so scrollback never detaches an image.
class KittyGraphics {
public:
  struct Pixels {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
  };
  struct Draw {
    std::shared_ptr<const Pixels> pixels;
    int x = 0, y = 0, width = 0, height = 0, sx = 0, sy = 0, sw = 0, sh = 0;
    int32_t z = 0;
    uint32_t image = 0, placement = 0;
    int clipTop = 0, clipBottom = 0;
  };
  struct Context {
    int column, row, top, rows, columns, cw, ch;
  };
  struct Result {
    std::string reply;
    int columns = 0, rows = 0;
  };
  struct Placeholder {
    uint32_t image, placement;
    int column, row, imageColumn, imageRow;
  };
  Result command(std::string_view, const Context &);
  std::vector<Draw> snapshot(int top, int rows, int cw, int ch,
                             const std::vector<Placeholder> & = {});
  void clearVisible(int top, int rows, int cw, int ch);
  void trim(int removed, int cw, int ch);
  void scroll(int top, int bottom, int amount, int cw, int ch);
  bool animate();
  void cancel() {
    pending_.clear();
    pending_control_.clear();
  }
  size_t imageCount() const { return images_.size(); }

private:
  using Control = std::map<char, int64_t>;
  struct Frame {
    std::shared_ptr<Pixels> pixels;
    int gap = 40;
  };
  struct Image {
    uint32_t number = 0;
    uint64_t age = 0;
    std::vector<Frame> frames;
    size_t current = 0;
    int state = 1, loops = 1, completed = 0;
    std::chrono::steady_clock::time_point next{};
  };
  struct Placement {
    uint32_t image = 0, id = 0, parent = 0, parentPlacement = 0;
    int column = 0, row = 0, columns = 0, rows = 0, x = 0, y = 0, sx = 0,
        sy = 0, sw = 0, sh = 0;
    int horizontal = 0, vertical = 0, clipTop = 0, clipBottom = 0;
    int32_t z = 0;
    bool virtual_ = false;
  };
  std::map<uint32_t, Image> images_;
  std::vector<Placement> placements_;
  std::vector<uint8_t> pending_;
  Control pending_control_;
  uint32_t next_ = 1;
  uint64_t age_ = 0;
  static constexpr size_t quota_ = 320u * 1024 * 1024;
  static int64_t get(const Control &, char, int64_t = 0);
  static Control parse(std::string_view);
  static std::vector<uint8_t> base64(std::string_view);
  static std::shared_ptr<Pixels> decode(const Control &, std::vector<uint8_t>);
  uint32_t identify(const Control &) const;
  size_t storage() const;
  void makeRoom(size_t, uint32_t);
  void removeOrphans();
  Draw draw(const Placement &, int, int, int, int) const;
  void compose(Pixels &, const Pixels &, int, int, int, int, int, int, bool);
};
