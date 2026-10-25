// SPDX-License-Identifier: GPL-3.0-or-later
#include "renderer.hpp"
#include <cassert>
#include <clocale>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <png.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <zlib.h>
static std::string b64(const std::vector<uint8_t> &bytes) {
  constexpr char chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < bytes.size(); i += 3) {
    unsigned v = unsigned(bytes[i]) << 16;
    if (i + 1 < bytes.size())
      v |= unsigned(bytes[i + 1]) << 8;
    if (i + 2 < bytes.size())
      v |= bytes[i + 2];
    out += chars[v >> 18];
    out += chars[(v >> 12) & 63];
    out += i + 1 < bytes.size() ? chars[(v >> 6) & 63] : '=';
    out += i + 2 < bytes.size() ? chars[v & 63] : '=';
  }
  return out;
}
static std::string b64(std::string text) {
  return b64(std::vector<uint8_t>(text.begin(), text.end()));
}
int main() {
  setlocale(LC_ALL, "C.UTF-8");
  unsetenv("DISPLAY");
  unsetenv("WAYLAND_DISPLAY");
  KittyGraphics g;
  KittyGraphics::Context c{0, 0, 0, 20, 80, 8, 16};
  auto cmd = [&](std::string s) { return g.command("G" + s, c); };
  assert(cmd("a=q,i=31,s=1,v=1,f=24;AAAA").reply == "\033_Gi=31;OK\033\\");
  assert(g.imageCount() == 0);
  assert(cmd("a=q,i=31,s=1,v=1,f=24;AA==").reply.find("EINVAL") !=
         std::string::npos);
  std::vector<uint8_t> red = {255, 0, 0, 255}, green = {0, 255, 0, 255};
  auto payload = b64(red);
  assert(cmd("a=T,i=1,s=1,v=1,c=2,r=2,C=1,m=1;" + payload.substr(0, 4))
             .reply.empty());
  assert(g.snapshot(0, 20, 8, 16).empty());
  assert(cmd("m=0;" + payload.substr(4)).reply == "\033_Gi=1;OK\033\\");
  auto view = g.snapshot(0, 20, 8, 16);
  assert(view.size() == 1 && view[0].pixels->rgba == red);
  assert(view[0].width == 16 && view[0].height == 16 &&
         view[0].y == 8); // aspect ratio
  assert(cmd("a=p,i=1,p=2,c=3,r=1,C=1").reply.find("OK") != std::string::npos);
  assert(cmd("a=p,i=1,p=2,c=4,r=1,C=1").reply.find("OK") != std::string::npos);
  assert(g.snapshot(0, 20, 8, 16).size() == 2);
  assert(cmd("a=p,i=999").reply.find("ENOENT") != std::string::npos);
  assert(cmd("a=p,i=999,q=2").reply.empty());
  assert(cmd("a=p,i=1,q=1,C=1").reply.empty());
  assert(cmd("a=p,i=1,p=3,P=1,Q=2,H=2,V=1").columns == 0);
  assert(g.snapshot(0, 20, 8, 16).size() == 4);
  assert(cmd("a=p,i=1,p=2,P=1,Q=3").reply.find("ECYCLE") != std::string::npos);
  assert(cmd("a=p,i=1,p=4,P=9,Q=9").reply.find("ENOPARENT") !=
         std::string::npos);
  cmd("a=d,d=i,i=1,p=2");
  assert(g.snapshot(0, 20, 8, 16).size() == 2); // child deleted
  // zlib, RGB, PNG and all local transfer media.
  uLongf length = compressBound(red.size());
  std::vector<uint8_t> compressed(length);
  assert(compress(compressed.data(), &length, red.data(), red.size()) == Z_OK);
  compressed.resize(length);
  assert(cmd("a=t,i=2,s=1,v=1,o=z;" + b64(compressed)).reply.find("OK") !=
         std::string::npos);
  png_image png{};
  png.version = PNG_IMAGE_VERSION;
  png.width = png.height = 1;
  png.format = PNG_FORMAT_RGBA;
  png_alloc_size_t size = 0;
  assert(png_image_write_to_memory(&png, nullptr, &size, 0, green.data(), 0,
                                   nullptr));
  std::vector<uint8_t> encoded(size);
  assert(png_image_write_to_memory(&png, encoded.data(), &size, 0, green.data(),
                                   0, nullptr));
  encoded.resize(size);
  assert(cmd("a=T,i=3,f=100,C=1;" + b64(encoded)).reply.find("OK") !=
         std::string::npos);
  char path[] = "/tmp/tty-graphics-protocol-test-XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  assert(write(fd, red.data(), red.size()) == 4);
  close(fd);
  assert(
      cmd("a=q,i=31,s=1,v=1,t=f;" + b64(std::string(path))).reply.find("OK") !=
      std::string::npos);
  assert(
      cmd("a=q,i=31,s=1,v=1,t=t;" + b64(std::string(path))).reply.find("OK") !=
      std::string::npos);
  assert(access(path, F_OK) != 0);
  auto shm = "/flterm-kitty-test-" + std::to_string(getpid());
  fd = shm_open(shm.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
  assert(fd >= 0);
  assert(ftruncate(fd, 4) == 0);
  assert(write(fd, red.data(), 4) == 4);
  close(fd);
  assert(cmd("a=q,i=31,s=1,v=1,t=s;" + b64(shm)).reply.find("OK") !=
         std::string::npos);
  assert(shm_open(shm.c_str(), O_RDONLY, 0) < 0);
  assert(cmd("a=q,i=31,s=1,v=1,t=f;" + b64(std::string("/dev/zero")))
             .reply.find("EBADF") != std::string::npos);
  // Numbers allocate IDs rather than replacing an older image with that number.
  auto first = cmd("I=7,s=1,v=1;" + payload).reply,
       second = cmd("I=7,s=1,v=1;" + payload).reply;
  assert(first != second && first.find(",I=7;OK") != std::string::npos);
  assert(cmd("a=p,I=7,C=1").reply.find("OK") != std::string::npos);
  // Root and additional frames, composition, explicit and timed animation.
  assert(cmd("a=f,i=1,s=1,v=1,z=10;" + b64(green)).reply.find("OK") !=
         std::string::npos);
  cmd("a=a,i=1,c=2");
  assert(g.snapshot(0, 20, 8, 16)[0].pixels->rgba == green);
  cmd("a=c,i=1,r=2,c=1,C=1");
  cmd("a=a,i=1,c=1");
  assert(g.snapshot(0, 20, 8, 16)[0].pixels->rgba == green);
  assert(cmd("a=c,i=1,r=1,c=1").reply.find("EINVAL") != std::string::npos);
  cmd("a=a,i=1,s=3,r=1,z=1,v=2");
  std::this_thread::sleep_for(std::chrono::milliseconds(3));
  assert(g.animate());
  cmd("a=d,d=f,i=1,r=2");
  // Virtual placements are invisible until referenced by a placeholder.
  cmd("a=p,i=2,p=9,U=1,c=2,r=1");
  auto before = g.snapshot(0, 20, 8, 16).size();
  assert(g.snapshot(0, 20, 8, 16, {{2, 9, 5, 5, 0, 0}}).size() == before + 1);
  // Parser integration, split APC, synchronized rendering, Unicode
  // placeholders.
  Fl_Term term(0, 0, 640, 320, nullptr, true);
  term.grid(80, 20);
  term.cellSize(8, 16);
  term.callback([](Fl_Widget *, void *) {});
  std::string control = "\033_Ga=T,i=42,s=1,v=1,C=1;" + payload + "\033\\";
  for (char ch : control)
    term.puts(&ch, 1);
  assert(term.snapshot().images.size() == 1);
  Renderer renderer;
  renderer.font({Renderer::defaultFont(), 16});
  renderer.render(term, 0, 1);
  size_t at = (size_t(renderer.height - 1) * renderer.width) * 4;
  assert(renderer.pixels[at] == 255 && renderer.pixels[at + 1] == 0);
  term.disp("\033[?1049h");
  assert(term.snapshot().images.empty());
  term.disp("\033[?1049l");
  assert(term.snapshot().images.size() == 1);
  term.disp("\033[?2026h\033_Ga=d\033\\");
  assert(term.snapshot().images.size() == 1);
  term.disp("\033[?2026l");
  assert(term.snapshot().images.empty());
  term.disp("\033_Ga=p,i=42,U=1,c=2,r=1\033\\\033[38;5;42m");
  term.disp("\xf4\x8e\xbb\xae\xcc\x85\xcc\x85\xf4\x8e\xbb\xae");
  assert(term.snapshot().images.size() == 2);
  assert(term.snapshot().cursorColumn == 2);
  term.disp("\033[2J");
  assert(term.snapshot().images.empty());
  std::string tmux = "\033Ptmux;";
  for (char ch : control) {
    tmux += ch;
    if (ch == '\033')
      tmux += ch;
  }
  tmux += "\033\\";
  for (char ch : tmux)
    term.puts(&ch, 1);
  assert(term.snapshot().images.size() == 1);
  std::cout
      << "PASS: Kitty queries, RGB/RGBA/PNG/zlib, chunks, files/temp/SHM, "
         "placements, aspect, IDs/numbers, quiet/errors, deletion, relative "
         "chains, animation/composition, placeholders, stacking, parser and "
         "alternate/synchronized screens\n";
}
