// SPDX-License-Identifier: GPL-3.0-or-later
#include "kitty-graphics.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <png.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
namespace {
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
constexpr size_t limit = 128u * 1024 * 1024;
bool dimensions(int64_t w, int64_t h) {
  return w > 0 && h > 0 && w <= 16384 && h <= 16384 &&
         w * h <= int64_t(limit / 4);
}
} // namespace
int64_t KittyGraphics::get(const Control &c, char k, int64_t fallback) {
  auto i = c.find(k);
  return i == c.end() ? fallback : i->second;
}
KittyGraphics::Control KittyGraphics::parse(std::string_view text) {
  Control result;
  while (!text.empty()) {
    auto comma = text.find(',');
    auto item = text.substr(0, comma);
    if (item.size() < 3 || item[1] != '=')
      fail("EINVAL:invalid control data");
    int64_t value = 0;
    if (std::string_view("atod").find(item[0]) != std::string_view::npos) {
      if (item.size() != 3)
        fail("EINVAL:invalid control character");
      value = (unsigned char)item[2];
    } else {
      auto number = item.substr(2);
      auto [p, ec] =
          std::from_chars(number.data(), number.data() + number.size(), value);
      if (ec != std::errc{} || p != number.data() + number.size() ||
          value < INT32_MIN || value > UINT32_MAX)
        fail("EINVAL:invalid integer");
      if (value > INT32_MAX &&
          std::string_view("iIpPQNY").find(item[0]) == std::string_view::npos)
        fail("EINVAL:integer outside supported range");
      if (value < 0 &&
          std::string_view("zHV").find(item[0]) == std::string_view::npos)
        fail("EINVAL:negative unsigned integer");
    }
    result[item[0]] = value;
    if (comma == text.npos)
      break;
    text.remove_prefix(comma + 1);
  }
  return result;
}
std::vector<uint8_t> KittyGraphics::base64(std::string_view text) {
  if (text.size() > limit * 4 / 3 + 4 || text.size() % 4)
    fail("EINVAL:invalid base64 size");
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<uint8_t> out;
  out.reserve(text.size() / 4 * 3);
  for (size_t i = 0; i < text.size(); i += 4) {
    unsigned v = 0;
    int padding = 0;
    for (size_t j = 0; j < 4; ++j) {
      char c = text[i + j];
      if (c == '=') {
        if (j < 2 || i + 4 != text.size())
          fail("EINVAL:invalid base64 padding");
        ++padding;
        v <<= 6;
      } else {
        auto at = alphabet.find(c);
        if (at == alphabet.npos || padding)
          fail("EINVAL:invalid base64");
        v = (v << 6) | at;
      }
    }
    if (padding > 2)
      fail("EINVAL:invalid base64 padding");
    out.push_back(v >> 16);
    if (padding < 2)
      out.push_back(v >> 8);
    if (!padding)
      out.push_back(v);
  }
  return out;
}
std::shared_ptr<KittyGraphics::Pixels>
KittyGraphics::decode(const Control &c, std::vector<uint8_t> bytes) {
  char medium = get(c, 't', 'd');
  if (medium != 'd') {
    std::string name(bytes.begin(), bytes.end());
    if (name.empty() || name.find('\0') != name.npos)
      fail("EINVAL:invalid filename");
    int fd = -1;
    if (medium == 's') {
      if (name[0] != '/' || name.find('/', 1) != name.npos)
        fail("EINVAL:invalid shared memory name");
      fd = shm_open(name.c_str(), O_RDONLY | O_CLOEXEC, 0);
    } else if (medium == 'f' || medium == 't')
      fd = open(name.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    else
      fail("EINVAL:unknown transmission medium");
    if (fd < 0)
      fail("ENOENT:cannot open image data");
    struct stat st{};
    auto offset = get(c, 'O');
    auto length = get(c, 'S');
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || offset > st.st_size ||
        st.st_size < 0) {
      close(fd);
      fail("EBADF:image data is not a regular file");
    }
    if (!length)
      length = st.st_size - offset;
    if (length > int64_t(limit) || length > st.st_size - offset) {
      close(fd);
      fail("EFBIG:image data exceeds limit");
    }
    bytes.resize(length);
    size_t readBytes = 0;
    while (readBytes < bytes.size()) {
      auto n = pread(fd, bytes.data() + readBytes, bytes.size() - readBytes,
                     offset + readBytes);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0) {
        close(fd);
        fail("EIO:short image read");
      }
      readBytes += n;
    }
    close(fd);
    if (medium == 's')
      shm_unlink(name.c_str());
    if (medium == 't' && name.find("tty-graphics-protocol") != name.npos) {
      std::error_code ec;
      auto path = std::filesystem::canonical(name, ec);
      std::vector<std::filesystem::path> roots = {"/tmp", "/var/tmp",
                                                  "/dev/shm"};
      if (auto tmp = getenv("TMPDIR"); tmp && *tmp)
        roots.emplace_back(tmp);
      for (auto &root : roots) {
        auto real = std::filesystem::canonical(root, ec);
        if (ec)
          continue;
        auto relative = path.lexically_relative(real);
        if (!relative.empty() && *relative.begin() != "..") {
          unlink(name.c_str());
          break;
        }
      }
    }
  }
  int format = get(c, 'f', 32);
  int64_t w = get(c, 's'), h = get(c, 'v');
  if (format != 24 && format != 32 && format != 100)
    fail("EINVAL:unknown pixel format");
  if (format != 100 && !dimensions(w, h))
    fail("EINVAL:invalid image dimensions");
  if (c.contains('o')) {
    if (get(c, 'o') != 'z')
      fail("EINVAL:unknown compression");
    size_t expected = format == 100 ? get(c, 'S') : w * h * (format / 8);
    if (!expected || expected > limit)
      fail("EINVAL:invalid decompressed size");
    std::vector<uint8_t> unpacked(expected);
    uLongf n = expected;
    if (uncompress(unpacked.data(), &n, bytes.data(), bytes.size()) != Z_OK ||
        n != expected)
      fail("EINVAL:invalid zlib data");
    bytes = std::move(unpacked);
  }
  auto result = std::make_shared<Pixels>();
  if (format == 100) {
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&png, bytes.data(), bytes.size()))
      fail("EINVAL:invalid PNG");
    if (!dimensions(png.width, png.height)) {
      png_image_free(&png);
      fail("EFBIG:PNG dimensions exceed limit");
    }
    png.format = PNG_FORMAT_RGBA;
    result->width = png.width;
    result->height = png.height;
    result->rgba.resize(PNG_IMAGE_SIZE(png));
    if (!png_image_finish_read(&png, nullptr, result->rgba.data(), 0,
                               nullptr)) {
      png_image_free(&png);
      fail("EINVAL:invalid PNG pixels");
    }
    png_image_free(&png);
  } else {
    if (bytes.size() != size_t(w * h * (format / 8)))
      fail("EINVAL:pixel data size mismatch");
    result->width = w;
    result->height = h;
    if (format == 32)
      result->rgba = std::move(bytes);
    else {
      result->rgba.resize(w * h * 4);
      for (size_t i = 0, j = 0; i < bytes.size(); i += 3, j += 4) {
        std::copy_n(bytes.data() + i, 3, result->rgba.data() + j);
        result->rgba[j + 3] = 255;
      }
    }
  }
  return result;
}
uint32_t KittyGraphics::identify(const Control &c) const {
  if (auto id = get(c, 'i'))
    return id;
  uint32_t result = 0;
  uint64_t age = 0;
  if (auto number = get(c, 'I'))
    for (auto &[id, image] : images_)
      if (image.number == number && image.age >= age) {
        result = id;
        age = image.age;
      }
  return result;
}
size_t KittyGraphics::storage() const {
  size_t bytes = 0;
  for (auto &[id, image] : images_)
    for (auto &frame : image.frames)
      bytes += frame.pixels->rgba.size();
  return bytes;
}
void KittyGraphics::makeRoom(size_t bytes, uint32_t preserve) {
  if (bytes > quota_)
    fail("ENOSPC:image exceeds storage quota");
  size_t used = storage();
  while (used + bytes > quota_) {
    auto victim = images_.end();
    bool victimPlaced = true;
    for (auto i = images_.begin(); i != images_.end(); ++i)
      if (i->first != preserve) {
        bool placed = std::any_of(placements_.begin(), placements_.end(),
                                  [&](auto &p) { return p.image == i->first; });
        if (victim == images_.end() || (victimPlaced && !placed) ||
            (placed == victimPlaced && i->second.age < victim->second.age)) {
          victim = i;
          victimPlaced = placed;
        }
      }
    if (victim == images_.end())
      fail("ENOSPC:animation exceeds storage quota");
    for (auto &f : victim->second.frames)
      used -= f.pixels->rgba.size();
    uint32_t id = victim->first;
    images_.erase(victim);
    std::erase_if(placements_, [&](auto &p) { return p.image == id; });
    removeOrphans();
  }
}
void KittyGraphics::removeOrphans() {
  bool changed;
  do {
    changed = false;
    std::erase_if(placements_, [&](auto &p) {
      bool orphan =
          p.parent &&
          std::none_of(placements_.begin(), placements_.end(), [&](auto &q) {
            return q.image == p.parent && q.id == p.parentPlacement;
          });
      changed |= orphan;
      return orphan;
    });
  } while (changed);
}
void KittyGraphics::compose(Pixels &dst, const Pixels &src, int sx, int sy,
                            int dx, int dy, int w, int h, bool replace) {
  if (sx < 0 || sy < 0 || dx < 0 || dy < 0 || w <= 0 || h <= 0 ||
      int64_t(sx) + w > src.width || int64_t(sy) + h > src.height ||
      int64_t(dx) + w > dst.width || int64_t(dy) + h > dst.height)
    fail("EINVAL:composition rectangle outside image");
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      auto from = &src.rgba[(size_t(sy + y) * src.width + sx + x) * 4];
      auto to = &dst.rgba[(size_t(dy + y) * dst.width + dx + x) * 4];
      if (replace)
        std::copy_n(from, 4, to);
      else {
        unsigned a = from[3], b = to[3] * (255 - a), alpha = a * 255 + b;
        for (int k = 0; k < 3; ++k)
          to[k] = alpha ? (from[k] * a * 255 + to[k] * b) / alpha : 0;
        to[3] = alpha / 255;
      }
    }
}
KittyGraphics::Draw KittyGraphics::draw(const Placement &p, int top, int rows,
                                        int cw, int ch) const {
  auto &im = images_.at(p.image);
  auto pixels = im.frames[im.current].pixels;
  int sw = std::min(p.sw ? p.sw : pixels->width, pixels->width - p.sx),
      sh = std::min(p.sh ? p.sh : pixels->height, pixels->height - p.sy);
  int width = p.columns ? p.columns * cw : sw,
      height = p.rows ? p.rows * ch : sh;
  if (p.columns && !p.rows)
    height = std::max(1, int(int64_t(sh) * width / sw));
  if (p.rows && !p.columns)
    width = std::max(1, int(int64_t(sw) * height / sh));
  int availableWidth = width, availableHeight = height;
  if (p.columns && p.rows) {
    double ratio = std::min(double(width) / sw, double(height) / sh);
    width = std::max(1, int(sw * ratio));
    height = std::max(1, int(sh * ratio));
  }
  return {pixels,
          p.column * cw + p.x + (availableWidth - width) / 2,
          (p.row - top) * ch + p.y + (availableHeight - height) / 2,
          width,
          height,
          p.sx,
          p.sy,
          sw,
          sh,
          p.z,
          p.image,
          p.id,
          std::max(0, (p.clipTop - top) * ch),
          p.clipBottom ? std::min(rows * ch, (p.clipBottom - top) * ch)
                       : rows * ch};
}
KittyGraphics::Result KittyGraphics::command(std::string_view sequence,
                                             const Context &ctx) {
  Result result;
  Control c;
  uint32_t id = 0;
  bool allocated = false;
  try {
    if (sequence.empty() || sequence.front() != 'G')
      return result;
    sequence.remove_prefix(1);
    auto semicolon = sequence.find(';');
    c = parse(sequence.substr(0, semicolon));
    auto data = semicolon == sequence.npos ? std::string_view{}
                                           : sequence.substr(semicolon + 1);
    char action = get(c, 'a', 't');
    if (get(c, 'q') > 2 || get(c, 'm') > 1)
      fail("EINVAL:invalid transfer flags");
    if (action == 'd')
      cancel();
    else if (!pending_control_.empty()) {
      auto continuation = c;
      c = pending_control_;
      for (auto [k, v] : continuation) {
        if (k != 'm' && k != 'q' && !(k == 'a' && v == 'f')) {
          cancel();
          fail("EINVAL:interleaved image transfer");
        }
        c[k] = v;
      }
      if (!continuation.contains('m'))
        c['m'] = 0;
      action = get(c, 'a', 't');
    }
    id = identify(c);
    if (get(c, 'i') && get(c, 'I'))
      fail("EINVAL:image id and number are mutually exclusive");
    if (action == 't' || action == 'T' || action == 'q' || action == 'f') {
      auto chunk = base64(data);
      if (pending_.size() + chunk.size() > limit) {
        cancel();
        fail("EFBIG:transfer exceeds limit");
      }
      pending_.insert(pending_.end(), chunk.begin(), chunk.end());
      if (get(c, 'm')) {
        pending_control_ = c;
        return result;
      }
      auto bytes = std::move(pending_);
      cancel();
      auto pixels = decode(c, std::move(bytes));
      if (action == 'q') {
        if (!get(c, 'i'))
          fail("EINVAL:query requires image id");
      } else if (action == 'f') {
        auto found = images_.find(id);
        if (found == images_.end())
          fail("ENOENT:image not found");
        auto &image = found->second;
        auto &root = *image.frames[0].pixels;
        int index = get(c, 'r');
        if (index > int(image.frames.size()))
          fail("ENOENT:frame not found");
        int base = get(c, 'c');
        if (base > int(image.frames.size()))
          fail("ENOENT:base frame not found");
        auto canvas = std::make_shared<Pixels>();
        canvas->width = root.width;
        canvas->height = root.height;
        if (index)
          canvas->rgba = image.frames[index - 1].pixels->rgba;
        else if (base)
          canvas->rgba = image.frames[base - 1].pixels->rgba;
        else {
          canvas->rgba.resize(root.rgba.size());
          uint32_t bg = get(c, 'Y');
          for (size_t i = 0; i < canvas->rgba.size(); i += 4)
            for (int k = 0; k < 4; ++k)
              canvas->rgba[i + k] = bg >> (24 - k * 8);
        }
        compose(*canvas, *pixels, 0, 0, get(c, 'x'), get(c, 'y'), pixels->width,
                pixels->height, get(c, 'X') == 1);
        makeRoom(index ? 0 : canvas->rgba.size(), id);
        int gap = get(c, 'z', 40);
        if (!gap)
          gap = index ? image.frames[index - 1].gap : 40;
        if (index)
          image.frames[index - 1] = {canvas, gap};
        else
          image.frames.push_back({canvas, gap});
      } else {
        if (!get(c, 'i')) {
          while (!next_ || images_.contains(next_))
            ++next_;
          id = next_++;
          allocated = true;
        }
        makeRoom(pixels->rgba.size(), id);
        std::erase_if(placements_, [&](auto &p) { return p.image == id; });
        removeOrphans();
        Image image;
        image.number = get(c, 'I');
        image.age = ++age_;
        image.frames.push_back({pixels, 0});
        images_[id] = std::move(image);
      }
    } else if (action != 'p' && action != 'd' && action != 'a' && action != 'c')
      fail("EINVAL:unknown graphics action");
    if (action == 'T' || action == 'p') {
      auto found = images_.find(id);
      if (found == images_.end())
        fail("ENOENT:image not found");
      if (placements_.size() >= 65536)
        fail("ENOSPC:too many placements");
      Placement p;
      p.image = id;
      p.id = get(c, 'p');
      p.column = ctx.column;
      p.row = ctx.row;
      p.columns = get(c, 'c');
      p.rows = get(c, 'r');
      p.x = get(c, 'X');
      p.y = get(c, 'Y');
      p.sx = get(c, 'x');
      p.sy = get(c, 'y');
      p.sw = get(c, 'w');
      p.sh = get(c, 'h');
      p.z = get(c, 'z');
      p.virtual_ = get(c, 'U') == 1;
      p.parent = get(c, 'P');
      p.parentPlacement = get(c, 'Q');
      p.horizontal = get(c, 'H');
      p.vertical = get(c, 'V');
      auto &pixels = *found->second.frames[0].pixels;
      for (char key : std::string_view("crxywhXY"))
        if (get(c, key) > 1000000)
          fail("EINVAL:placement dimensions exceed limit");
      if (p.sx >= pixels.width || p.sy >= pixels.height || p.x >= ctx.cw ||
          p.y >= ctx.ch)
        fail("EINVAL:placement outside image or cell");
      if (p.virtual_ && (p.parent || !p.columns || !p.rows))
        fail("EINVAL:invalid virtual placement");
      if (p.parent) {
        uint32_t parent = p.parent, placement = p.parentPlacement;
        int depth = 0;
        for (;;) {
          if (parent == p.image && placement == p.id && p.id)
            fail("ECYCLE:relative placement cycle");
          auto q = std::find_if(
              placements_.begin(), placements_.end(),
              [&](auto &v) { return v.image == parent && v.id == placement; });
          if (q == placements_.end())
            fail("ENOPARENT:parent placement not found");
          if (++depth > 32)
            fail("ETOODEEP:relative placement chain exceeds limit");
          if (!q->parent)
            break;
          parent = q->parent;
          placement = q->parentPlacement;
        }
      }
      if (p.id)
        std::erase_if(placements_, [&](auto &q) {
          return q.image == p.image && q.id == p.id;
        });
      placements_.push_back(p);
      if (!get(c, 'C') && !p.virtual_ && !p.parent) {
        auto d = draw(p, ctx.top, ctx.rows, ctx.cw, ctx.ch);
        result.columns =
            p.columns ? p.columns : (d.width + p.x + ctx.cw - 1) / ctx.cw;
        result.rows = p.rows ? p.rows : (d.height + p.y + ctx.ch - 1) / ctx.ch;
      }
    } else if (action == 'd') {
      char mode = get(c, 'd', 'a'), kind = std::tolower((unsigned char)mode);
      bool free = std::isupper((unsigned char)mode);
      if (std::string_view("aincpqrxyzf").find(kind) == std::string_view::npos)
        fail("EINVAL:invalid delete mode");
      if (kind == 'f') {
        auto found = images_.find(id);
        if (found == images_.end())
          fail("ENOENT:image not found");
        auto &image = found->second;
        int index = get(c, 'r', 1);
        if (index < 1 || index > int(image.frames.size()))
          fail("ENOENT:frame not found");
        if (image.frames.size() > 1)
          image.frames.erase(image.frames.begin() + index - 1);
        image.current = std::min(image.current, image.frames.size() - 1);
      } else {
        std::vector<uint32_t> removed;
        std::erase_if(placements_, [&](auto &p) {
          auto d = draw(p, ctx.top, ctx.rows, ctx.cw, ctx.ch);
          bool visible = d.y + d.height > 0 && d.y < ctx.rows * ctx.ch;
          int x = get(c, 'x', 1) - 1, y = get(c, 'y', 1) - 1;
          if (kind == 'c') {
            x = ctx.column;
            y = ctx.row - ctx.top;
          }
          bool inX = x * ctx.cw < d.x + d.width && (x + 1) * ctx.cw > d.x;
          bool inY = y * ctx.ch < d.y + d.height && (y + 1) * ctx.ch > d.y;
          bool match = (kind == 'a' && visible) ||
                       ((kind == 'i' || kind == 'n') && p.image == id &&
                        (!get(c, 'p') || p.id == get(c, 'p'))) ||
                       (kind == 'r' && p.image >= get(c, 'x') &&
                        p.image <= get(c, 'y')) ||
                       ((kind == 'c' || kind == 'p' || kind == 'q') && inX &&
                        inY && (kind != 'q' || p.z == get(c, 'z'))) ||
                       (kind == 'x' && inX) || (kind == 'y' && inY) ||
                       (kind == 'z' && p.z == get(c, 'z'));
          if (p.virtual_ && kind != 'i' && kind != 'n' && kind != 'r')
            match = false;
          if (match)
            removed.push_back(p.image);
          return match;
        });
        if (free) {
          if ((kind == 'i' || kind == 'n') && id)
            removed.push_back(id);
          if (kind == 'r')
            for (auto &[key, image] : images_)
              if (key >= get(c, 'x') && key <= get(c, 'y'))
                removed.push_back(key);
          for (auto key : removed)
            if (std::none_of(placements_.begin(), placements_.end(),
                             [&](auto &p) { return p.image == key; }))
              images_.erase(key);
        }
        removeOrphans();
      }
    } else if (action == 'a' || action == 'c') {
      auto found = images_.find(id);
      if (found == images_.end())
        fail("ENOENT:image not found");
      auto &image = found->second;
      auto frame = [&](int n) -> Frame & {
        if (n < 1 || n > int(image.frames.size()))
          fail("ENOENT:frame not found");
        return image.frames[n - 1];
      };
      if (action == 'a') {
        if (auto n = get(c, 'c'))
          image.current = &frame(n) - image.frames.data();
        if (auto n = get(c, 'r'); n && get(c, 'z'))
          frame(n).gap = get(c, 'z');
        if (auto state = get(c, 's')) {
          if (state > 3)
            fail("EINVAL:invalid animation state");
          image.state = state;
          if (state == 1)
            image.completed = 0;
        }
        if (auto loops = get(c, 'v'))
          image.loops = loops;
        image.next = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(
                         std::max(0, image.frames[image.current].gap));
      } else {
        auto source = frame(get(c, 'r')).pixels;
        auto &destination = frame(get(c, 'c'));
        int sx = get(c, 'X'), sy = get(c, 'Y'), dx = get(c, 'x'),
            dy = get(c, 'y'), w = get(c, 'w', source->width),
            h = get(c, 'h', source->height);
        if (source == destination.pixels && dx < sx + w && sx < dx + w &&
            dy < sy + h && sy < dy + h)
          fail("EINVAL:overlapping frame composition");
        auto canvas = std::make_shared<Pixels>(*destination.pixels);
        compose(*canvas, *source, sx, sy, dx, dy, w, h, get(c, 'C') == 1);
        destination.pixels = canvas;
      }
    }
    if ((get(c, 'i') || get(c, 'I') || action == 'q') && get(c, 'q') == 0 &&
        action != 'd') {
      result.reply = "\033_Gi=" + std::to_string(id);
      if (get(c, 'I'))
        result.reply += ",I=" + std::to_string(get(c, 'I'));
      if (get(c, 'p'))
        result.reply += ",p=" + std::to_string(get(c, 'p'));
      result.reply += ";OK\033\\";
    }
  } catch (const std::exception &e) {
    cancel();
    result.columns = result.rows = 0;
    if (get(c, 'q') != 2 && (get(c, 'i') || get(c, 'I'))) {
      result.reply = "\033_Gi=" + std::to_string(id ? id : get(c, 'i'));
      if (get(c, 'I'))
        result.reply += ",I=" + std::to_string(get(c, 'I'));
      if (get(c, 'p'))
        result.reply += ",p=" + std::to_string(get(c, 'p'));
      result.reply += ';';
      result.reply += e.what();
      result.reply += "\033\\";
    }
    if (allocated) {
      images_.erase(id);
      std::erase_if(placements_, [&](auto &p) { return p.image == id; });
      removeOrphans();
    }
  }
  return result;
}
bool KittyGraphics::animate() {
  bool changed = false;
  auto now = std::chrono::steady_clock::now();
  for (auto &[id, image] : images_) {
    if (image.state == 1 || image.frames.size() < 2 || now < image.next)
      continue;
    for (size_t step = 0; step < image.frames.size(); ++step) {
      if (image.current + 1 == image.frames.size()) {
        if (image.state == 2)
          break;
        if (image.loops > 1 && ++image.completed >= image.loops - 1) {
          image.state = 1;
          break;
        }
        image.current = 0;
      } else
        ++image.current;
      changed = true;
      int gap = image.frames[image.current].gap;
      image.next = now + std::chrono::milliseconds(std::max(1, gap));
      if (gap >= 0)
        break;
    }
  }
  return changed;
}
std::vector<KittyGraphics::Draw>
KittyGraphics::snapshot(int top, int rows, int cw, int ch,
                        const std::vector<Placeholder> &placeholders) {
  animate();
  std::vector<Draw> result;
  // Resolve real and virtual parents before following relative chains.
  struct Resolved {
    const Placement *placement;
    int column, row;
  };
  std::vector<Resolved> resolved;
  std::vector<const Placement *> waiting;
  for (auto &p : placements_) {
    if (!images_.contains(p.image))
      continue;
    if (p.parent) {
      waiting.push_back(&p);
      continue;
    }
    if (!p.virtual_) {
      resolved.push_back({&p, p.column, p.row});
      continue;
    }
    bool anchored = false;
    int left = INT_MAX, upper = INT_MAX;
    for (auto &h : placeholders)
      if (h.image == p.image && (!h.placement || h.placement == p.id) &&
          h.imageColumn < p.columns && h.imageRow < p.rows) {
        auto d = draw(p, top, rows, cw, ch);
        int fullWidth = p.columns * cw, fullHeight = p.rows * ch;
        // Crop one cell from the aspect-preserving virtual canvas.
        int cellLeft = h.imageColumn * cw, cellTop = h.imageRow * ch;
        int imageLeft = (fullWidth - d.width) / 2,
            imageTop = (fullHeight - d.height) / 2;
        int x0 = std::max(cellLeft, imageLeft),
            y0 = std::max(cellTop, imageTop);
        int x1 = std::min(cellLeft + cw, imageLeft + d.width),
            y1 = std::min(cellTop + ch, imageTop + d.height);
        if (x1 > x0 && y1 > y0) {
          int sx = d.sx + (int64_t(x0 - imageLeft) * d.sw) / d.width,
              sy = d.sy + (int64_t(y0 - imageTop) * d.sh) / d.height;
          int sw = std::max(1, int(int64_t(x1 - imageLeft) * d.sw / d.width) -
                                   sx + d.sx),
              sh = std::max(1, int(int64_t(y1 - imageTop) * d.sh / d.height) -
                                   sy + d.sy);
          d.x = h.column * cw + x0 - cellLeft;
          d.y = h.row * ch + y0 - cellTop;
          d.width = x1 - x0;
          d.height = y1 - y0;
          d.sx = sx;
          d.sy = sy;
          d.sw = sw;
          d.sh = sh;
          result.push_back(d);
        }
        anchored = true;
        left = std::min(left, h.column - h.imageColumn);
        upper = std::min(upper, h.row + top - h.imageRow);
      }
    if (anchored)
      resolved.push_back({&p, left, upper});
  }
  for (int depth = 0; depth < 32 && !waiting.empty(); ++depth) {
    for (auto i = waiting.begin(); i != waiting.end();) {
      auto p = *i;
      auto parent =
          std::find_if(resolved.begin(), resolved.end(), [&](auto &r) {
            return r.placement->image == p->parent &&
                   r.placement->id == p->parentPlacement;
          });
      if (parent == resolved.end()) {
        ++i;
        continue;
      }
      int x = parent->column + p->horizontal, y = parent->row + p->vertical;
      resolved.push_back({p, x, y});
      i = waiting.erase(i);
    }
  }
  for (auto &r : resolved)
    if (!r.placement->virtual_) {
      auto p = *r.placement;
      p.column = r.column;
      p.row = r.row;
      auto d = draw(p, top, rows, cw, ch);
      if (d.y + d.height > 0 && d.y < rows * ch)
        result.push_back(d);
    }
  std::stable_sort(result.begin(), result.end(), [](auto &a, auto &b) {
    return std::pair{a.z, a.image} < std::pair{b.z, b.image};
  });
  return result;
}
void KittyGraphics::clearVisible(int top, int rows, int cw, int ch) {
  std::erase_if(placements_, [&](auto &p) {
    auto d = draw(p, top, rows, cw, ch);
    return !p.virtual_ && !p.parent && d.y + d.height > 0 && d.y < rows * ch;
  });
  removeOrphans();
  cancel();
}
void KittyGraphics::trim(int removed, int cw, int ch) {
  for (auto &p : placements_)
    if (!p.parent && !p.virtual_) {
      p.row -= removed;
      if (p.clipTop)
        p.clipTop -= removed;
      if (p.clipBottom)
        p.clipBottom -= removed;
    }
  std::erase_if(placements_, [&](auto &p) {
    return !p.virtual_ && !p.parent &&
           draw(p, 0, 1, cw, ch).y + draw(p, 0, 1, cw, ch).height <= 0;
  });
  removeOrphans();
}
void KittyGraphics::scroll(int top, int bottom, int amount, int cw, int ch) {
  for (auto &p : placements_)
    if (!p.virtual_ && !p.parent) {
      auto d = draw(p, 0, bottom + 1, cw, ch);
      if ((p.row >= top && p.row + (d.height + ch - 1) / ch <= bottom + 1) ||
          (p.clipTop == top && p.clipBottom == bottom + 1)) {
        p.row -= amount;
        p.clipTop = top;
        p.clipBottom = bottom + 1;
      }
    }
  std::erase_if(placements_, [&](auto &p) {
    auto d = draw(p, 0, bottom + 1, cw, ch);
    return !p.virtual_ && !p.parent &&
           (d.y + d.height <= d.clipTop || d.y >= d.clipBottom);
  });
  removeOrphans();
}
