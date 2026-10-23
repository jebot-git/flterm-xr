// SPDX-License-Identifier: GPL-3.0-or-later
#include "appearance.h"
#include "background.hpp"
#include "clipboard.hpp"
#include "control.hpp"
#include "desktop-menu.h"
#include "desktop-keyboard.hpp"
#include "keyboard.hpp"
#include "layout-state.hpp"
#include "pointer.hpp"
#include "renderer.hpp"
#include "xr.hpp"
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/fl_utf8.h>
#include <FL/gl.h>
#include <FL/platform.H>
#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <clocale>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

static volatile sig_atomic_t stopped = 0, toggleRequested = 0;
static void signalHandler(int s) {
  if (s == SIGUSR1)
    toggleRequested = 1;
  else
    stopped = 1;
}
struct Options {
  bool desktop = false, wayland = false, headless = false, visible = true,
       noKeyboard = false, persist = true, standalone = false;
  bool sizeExplicit = false, widthExplicit = false, heightExplicit = false;
  bool xExplicit = false, yExplicit = false, zExplicit = false;
  int cols = 100, rows = 30, tabs = 1, pixels = 16;
  float seconds = 0, opacity = 0, styledOpacity = 1;
  uint32_t placement = 20;
  PanelLayout layout{1.2f, .6f, 0, -.15f, -1.2f};
  std::string font, blend = "auto", screenshot, report;
  std::filesystem::path layoutFile;
  std::vector<std::string> command;
};
static void help() {
  std::cout
      << "FLTerm XR — native FLTK Wayland terminal and persistent OpenXR "
         "overlay\n"
         "flterm-xr [options] [-- command args...]\n"
         " --wayland              Independent native FLTK Wayland window; no "
         "XR/input grab\n"
         " --preview-only         FLTK diagnostic desktop window; no OpenXR\n"
         " --headless             Control/PTY test mode; no display, XR or "
         "input devices\n"
         " --probe                Print OpenXR capabilities without creating a "
         "terminal\n"
         " --hidden --no-keyboard Start hidden / disable native keyboard and "
         "pointer capture\n"
         " --standalone           Explicit primary XR session instead of "
         "overlay\n"
         " --cols N --rows N --tabs N      Grid and independent shells (up to "
         "16 tabs)\n"
         " --font FILE --font-size N       Native BDF font; inherits DomeD "
         "Appearance\n"
         " --width M --height M --position X Y Z --distance M --vertical M\n"
         " --background-opacity A --styled-background-opacity A  Alpha in "
         "[0,1]\n"
         " --blend auto|opaque|alpha|additive --placement N\n"
         " --layout-file FILE --no-save-layout  Overlay persistence only\n"
         " --seconds N --screenshot FILE --report FILE  Bounded validation\n"
         " Ctrl+Shift+T/W: new/close tab; Ctrl+PageUp/PageDown: switch tabs.\n"
         " Shift+PageUp/PageDown: scroll history; Shift+Home/End: oldest/live.\n"
         " Ctrl+Shift+C/V/A: copy/paste/select all; F12 hides XR, Super+F12 "
         "shows it.\n"
         " XR: Ctrl+Alt+arrows move; add Shift to resize; PageUp/Down adjust "
         "depth;\n"
         " +/- scales, Home resets, B toggles transparent/30% background.\n";
}
static Options parse(int argc, char **argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto value = [&]() {
      if (++i >= argc)
        throw std::runtime_error("Missing value for " + arg);
      return std::string(argv[i]);
    };
    if (arg == "--") {
      for (++i; i < argc; ++i)
        o.command.emplace_back(argv[i]);
      break;
    }
    if (arg == "--help" || arg == "-h") {
      help();
      exit(0);
    } else if (arg == "--probe") {
      XrOverlay::probe();
      exit(0);
    } else if (arg == "--wayland") {
      o.desktop = o.wayland = true;
    } else if (arg == "--preview-only")
      o.desktop = true;
    else if (arg == "--headless")
      o.headless = true;
    else if (arg == "--no-window") {
    } else if (arg == "--hidden")
      o.visible = false;
    else if (arg == "--no-keyboard")
      o.noKeyboard = true;
    else if (arg == "--standalone")
      o.standalone = true;
    else if (arg == "--no-save-layout")
      o.persist = false;
    else if (arg == "--layout-file")
      o.layoutFile = value();
    else if (arg == "--font") {
      o.font = value();
    } else if (arg == "--font-size") {
      o.pixels = std::stoi(value());
      o.sizeExplicit = true;
    } else if (arg == "--cols")
      o.cols = std::stoi(value());
    else if (arg == "--rows")
      o.rows = std::stoi(value());
    else if (arg == "--tabs")
      o.tabs = std::stoi(value());
    else if (arg == "--width") {
      o.layout.width = std::stof(value());
      o.widthExplicit = true;
    } else if (arg == "--height") {
      o.layout.height = std::stof(value());
      o.heightExplicit = true;
    } else if (arg == "--position") {
      o.layout.x = std::stof(value());
      o.layout.y = std::stof(value());
      o.layout.z = std::stof(value());
      o.xExplicit = o.yExplicit = o.zExplicit = true;
    } else if (arg == "--distance") {
      o.layout.z = -std::stof(value());
      o.zExplicit = true;
    } else if (arg == "--vertical") {
      o.layout.y = std::stof(value());
      o.yExplicit = true;
    } else if (arg == "--seconds")
      o.seconds = std::stof(value());
    else if (arg == "--background-opacity")
      o.opacity = std::stof(value());
    else if (arg == "--styled-background-opacity")
      o.styledOpacity = std::stof(value());
    else if (arg == "--placement") {
      int v = std::stoi(value());
      if (v <= 0)
        throw std::runtime_error("Placement must be positive");
      o.placement = v;
    } else if (arg == "--blend")
      o.blend = value();
    else if (arg == "--screenshot")
      o.screenshot = value();
    else if (arg == "--report")
      o.report = value();
    else
      throw std::runtime_error("Unknown option: " + arg);
  }
  auto &p = o.layout;
  if (o.cols < 10 || o.cols > 240 || o.rows < 2 || o.rows > 100 || o.tabs < 1 ||
      o.tabs > 16 || o.pixels < 8 || o.pixels > 64 ||
      !std::isfinite(o.seconds) || o.seconds < 0 || !std::isfinite(o.opacity) ||
      o.opacity < 0 || o.opacity > 1 || !std::isfinite(o.styledOpacity) ||
      o.styledOpacity < 0 || o.styledOpacity > 1 || !std::isfinite(p.width) ||
      p.width < .05f || p.width > 10 || !std::isfinite(p.height) ||
      p.height < .05f || p.height > 10 || !std::isfinite(p.x) ||
      abs(p.x) > 10 || !std::isfinite(p.y) || abs(p.y) > 10 ||
      !std::isfinite(p.z) || p.z > -.2f || p.z < -20 ||
      (o.blend != "auto" && o.blend != "alpha" && o.blend != "opaque" &&
       o.blend != "additive") ||
      (o.headless && o.desktop))
    throw std::runtime_error("Invalid terminal options");
  if (o.desktop)
    o.persist = false;
  return o;
}
struct App {
  Options options;
  Renderer renderer;
  DomeFont inherited;
  std::vector<std::unique_ptr<Terminal>> tabs;
  size_t active = 0;
  bool dirty = true, visible = true;
  bool selecting = false;
  std::string menuService, menuPath;
  PanelLayout layout, initial;
  LayoutState state;
  LayoutShortcuts layoutKeys;
  BackgroundShortcut backgroundKey;
  std::array<bool, SDL_NUM_SCANCODES> consumed{};
  std::unique_ptr<NativeGraphics> graphics;
  std::unique_ptr<XrOverlay> xr;
  std::unique_ptr<NativeKeyboard> keyboard;
  std::unique_ptr<DesktopKeyboard> desktopKeyboard;
  struct DesktopKey { unsigned symbol; SDL_KeyboardEvent event; std::string text; };
  std::vector<DesktopKey> desktopKeys;
  std::unique_ptr<NativePointer> pointer;
  Clipboard clipboard{false};
  uint64_t pasteTab = 0, nextTab = 1;
  std::map<Terminal *, uint64_t> identities;
  explicit App(Options o)
      : options(std::move(o)),
        inherited(options.font, options.pixels, options.sizeExplicit),
        visible(options.visible), layout(options.layout), initial(layout),
        state(options.layoutFile.empty() ? LayoutState::defaultPath()
                                         : options.layoutFile) {
    renderer.opacity = options.desktop ? 1 : options.opacity;
    renderer.styledOpacity = options.styledOpacity;
    renderer.font(inherited.read().value_or(FontChoice{
        options.font.empty() ? Renderer::defaultFont() : options.font,
        options.pixels}));
    if (!options.heightExplicit)
      layout.height = layout.width * float((options.rows + 1) * renderer.ch) /
                      (options.cols * renderer.cw);
    if (options.persist)
      if (auto saved = state.load()) {
        if (!options.widthExplicit)
          layout.width = saved->width;
        if (!options.heightExplicit)
          layout.height = saved->height;
        if (!options.xExplicit)
          layout.x = saved->x;
        if (!options.yExplicit)
          layout.y = saved->y;
        if (!options.zExplicit)
          layout.z = saved->z;
      }
    initial = layout;
    for (int i = 0; i < options.tabs; ++i)
      add(i ? std::vector<std::string>{} : options.command);
    appearance();
    render();
  }
  Terminal &term() { return *tabs.at(active); }
  void add(const std::vector<std::string> &command = {}) {
    if (tabs.size() >= 16)
      return;
    auto group = Fl_Group::current();
    Fl_Group::current(nullptr);
    auto t = std::make_unique<Terminal>(options.cols, options.rows, command);
    Fl_Group::current(group);
    t->cellSize(renderer.cw, renderer.ch);
    identities[t.get()] = nextTab++;
    tabs.push_back(std::move(t));
    active = tabs.size() - 1;
    renderer.preedit.clear();
    dirty = true;
  }
  void select(size_t index) {
    if (index < tabs.size()) {
      active = index;
      renderer.preedit.clear();
      selecting = false;
      dirty = true;
    }
  }
  void close() {
    if (tabs.empty())
      return;
    identities.erase(tabs[active].get());
    tabs.erase(tabs.begin() + active);
    if (!tabs.empty())
      active = std::min(active, tabs.size() - 1);
    dirty = true;
  }
  void appearance() {
    if (auto font = inherited.read(); font && *font != renderer.choice) {
      renderer.font(*font);
      for (auto &t : tabs)
        t->cellSize(renderer.cw, renderer.ch);
      dirty = true;
      if (xr)
        xr->resize(options.cols * renderer.cw,
                   (options.rows + 1) * renderer.ch);
      if (pointer)
        pointer->resize(options.cols * renderer.cw, options.rows * renderer.ch);
    }
    try {
      auto name = getenv("XDG_CURRENT_DESKTOP");
      if (!name || std::string(name).find("Dome") == std::string::npos)
        return;
      auto directory = getenv("XDG_CONFIG_HOME");
      auto home = getenv("HOME");
      auto path = std::filesystem::path(directory ? directory
                                        : home ? std::string(home) + "/.config"
                                               : "/tmp") /
                  "dome/appearance.json";
      std::ifstream input(path);
      if (!input)
        return;
      auto config = Json::parse(input);
      bool light = config.value("light", false);
      if (light != renderer.light) {
        renderer.light = light;
        dirty = true;
      }
      auto color = config.value("accent", std::string("#6babff"));
      if (color.size() == 7 && color[0] == '#') {
        std::array<uint8_t, 3> accent{};
        for (int c = 0; c < 3; ++c)
          accent[c] = std::stoul(color.substr(c * 2 + 1, 2), nullptr, 16);
        if (accent != renderer.accent) {
          renderer.accent = accent;
          dirty = true;
        }
      }
    } catch (const std::exception &error) {
      std::cerr << "Appearance unchanged: " << error.what() << '\n';
    }
  }
  void render() {
    if (!tabs.empty()) {
      renderer.render(term(), active, tabs.size());
      dirty = false;
    }
  }
  bool key(const SDL_KeyboardEvent &event, const std::string &text = {},
           const kitty_keyboard::Key *identity = nullptr) {
    auto sc = event.keysym.scancode;
    auto mod = event.keysym.mod;
    bool down = event.type == SDL_KEYDOWN;
    if (sc >= 0 && sc < SDL_NUM_SCANCODES && !down && consumed[sc]) {
      consumed[sc] = false;
      return true;
    }
    auto consume = [&]() {
      if (down && sc >= 0 && sc < SDL_NUM_SCANCODES)
        consumed[sc] = true;
      return true;
    };
    if (!options.desktop) {
      if (layoutKeys.handle(event, layout, initial))
        return true;
      if (backgroundKey.handle(event, renderer.opacity)) {
        dirty = true;
        return true;
      }
      if (sc == SDL_SCANCODE_F12 &&
          !(mod & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT))) {
        if (down && !event.repeat)
          visible = (mod & KMOD_GUI) ? !visible : false;
        return consume();
      }
    }
    bool ctrl = mod & KMOD_CTRL, shift = mod & KMOD_SHIFT, alt = mod & KMOD_ALT;
    if (shift && !ctrl && !alt && !(mod & KMOD_GUI) && !tabs.empty() &&
        (sc == SDL_SCANCODE_PAGEUP || sc == SDL_SCANCODE_PAGEDOWN ||
         sc == SDL_SCANCODE_HOME || sc == SDL_SCANCODE_END)) {
      if (down) {
        if (sc == SDL_SCANCODE_END) term().scrollToBottom();
        else term().scrollRows(sc == SDL_SCANCODE_HOME ? -10000 :
                             (sc == SDL_SCANCODE_PAGEUP ? -1 : 1) * (term().sizeY()-1));
        dirty = true;
      }
      return consume();
    }
    if (ctrl && !alt && !(mod & KMOD_GUI)) {
      if ((shift && (sc == SDL_SCANCODE_T || sc == SDL_SCANCODE_W)) ||
          sc == SDL_SCANCODE_PAGEUP || sc == SDL_SCANCODE_PAGEDOWN) {
        if (down && !event.repeat) {
          if (sc == SDL_SCANCODE_T)
            add();
          else if (sc == SDL_SCANCODE_W)
            close();
          else if (!tabs.empty())
            select(
                (active + (sc == SDL_SCANCODE_PAGEUP ? tabs.size() - 1 : 1)) %
                tabs.size());
        }
        return consume();
      }
      if (shift && (sc == SDL_SCANCODE_C || sc == SDL_SCANCODE_V ||
                    sc == SDL_SCANCODE_A)) {
        if (down && !event.repeat) {
          if (sc == SDL_SCANCODE_A) {
            term().selectAll();
            dirty = true;
          } else if (sc == SDL_SCANCODE_C) {
            auto selected = term().selectionText();
            if (options.desktop)
              Fl::copy(selected.data(), selected.size(), 1);
            else
              clipboard.copy(selected);
          } else if (options.desktop) { /* Canvas handles toolkit paste. */
            return false;
          } else {
            pasteTab = identities[&term()];
            clipboard.paste();
          }
        }
        return consume();
      }
    }
    if (tabs.empty())
      return true;
    if (down && !renderer.preedit.empty() && sc == SDL_SCANCODE_ESCAPE) {
      renderer.preedit.clear();
      dirty = true;
      return true;
    }
    auto translated = identity ? *identity : inputKey(event,text);
    auto bytes = kitty_keyboard::encode(translated,term().keyboardFlags(),term().applicationCursor());
    if (!bytes.empty()) {
      term().scrollToBottom();
      term().write(bytes.data(), bytes.size());
      dirty = true;
    }
    return true;
  }
  Json status() const {
    return {{"backend", "FLTerm"},
            {"mode", options.desktop    ? "wayland"
                     : options.headless ? "headless"
                                        : "overlay"},
            {"menu_service", menuService},
            {"menu_path", menuPath},
            {"visible", visible},
            {"tabs", tabs.size()},
            {"active", active + 1},
            {"cols", options.cols},
            {"rows", options.rows},
            {"history_rows", tabs.empty() ? 0 : tabs[active]->historyRows()},
            {"scroll_offset", tabs.empty() ? 0 : tabs[active]->scrollOffset()},
            {"font", renderer.choice.path},
            {"font_pixels", renderer.ch},
            {"light", renderer.light},
            {"opacity", renderer.opacity},
            {"layout",
             {{"width", layout.width},
              {"height", layout.height},
              {"x", layout.x},
              {"y", layout.y},
              {"z", layout.z}}},
            {"xr_running", xr && xr->running},
            {"visible_frames", xr ? xr->visibleFrames : 0},
            {"hidden_frames", xr ? xr->hiddenFrames : 0}};
  }
  void control(Control &control) {
    char message[65536];
    for (int i = 0; i < 64; ++i) {
      sockaddr_un peer{};
      socklen_t peerSize = sizeof(peer);
      auto n = recvfrom(control.fd, message, sizeof(message), 0,
                        reinterpret_cast<sockaddr *>(&peer), &peerSize);
      if (n < 0)
        break;
      std::string msg(message, n);
      if (msg == "status") {
        auto text = status().dump();
        sendto(control.fd, text.data(), text.size(), MSG_NOSIGNAL,
               reinterpret_cast<sockaddr *>(&peer), peerSize);
        continue;
      }
      if (msg == "show" || msg == "focus")
        visible = true;
      else if (msg == "hide")
        visible = false;
      else if (msg == "toggle")
        visible = !visible;
      else if (msg == "quit")
        stopped = 1;
      else if (msg == "tab-new")
        add();
      else if (msg == "tab-close")
        close();
      else if (msg == "tab-next" && !tabs.empty())
        select((active + 1) % tabs.size());
      else if (msg == "tab-prev" && !tabs.empty())
        select((active + tabs.size() - 1) % tabs.size());
      else if (msg.starts_with("tab-select ")) {
        std::istringstream in(msg.substr(11));
        int index = 0;
        if (in >> index && index > 0)
          select(index - 1);
      } else if (msg.starts_with("type ") && !tabs.empty()) {
        term().scrollToBottom();
        term().write(msg.data() + 5, msg.size() - 5);
      } else if (!options.desktop) {
        std::istringstream in(msg.substr(msg.find(' ') + 1));
        float a = 0, b = 0, c = 0;
        if (msg == "reset-layout")
          layout = initial;
        else if (msg.starts_with("position ") && in >> a >> b >> c &&
                 std::isfinite(a) && std::isfinite(b) && std::isfinite(c) &&
                 abs(a) <= 10 && abs(b) <= 10 && c <= -.2f && c >= -20) {
          layout.x = a;
          layout.y = b;
          layout.z = c;
        } else if (msg.starts_with("size ") && in >> a >> b &&
                   std::isfinite(a) && std::isfinite(b) && a >= .05f &&
                   a <= 10 && b >= .05f && b <= 10) {
          layout.width = a;
          layout.height = b;
        } else if (msg.starts_with("move ") && in >> a >> b >> c &&
                   std::isfinite(a) && std::isfinite(b) && std::isfinite(c))
          layout.move(a, b, c);
        else if (msg.starts_with("resize ") && in >> a >> b &&
                 std::isfinite(a) && std::isfinite(b))
          layout.resize(a, b);
        else if (msg.starts_with("scale ") && in >> a && std::isfinite(a) &&
                 a > 0)
          layout.scale(a);
        else if (msg.starts_with("opacity ") && in >> a && std::isfinite(a) &&
                 a >= 0 && a <= 1)
          renderer.opacity = a;
      }
      dirty = true;
      if (tabs.empty())
        break;
    }
  }
  void pump() {
    for (size_t i = 0; i < tabs.size();) {
      dirty = tabs[i]->pty->pump() || dirty;
      if (!tabs[i]->pty->live()) {
        identities.erase(tabs[i].get());
        tabs.erase(tabs.begin() + i);
        dirty = true;
      } else
        ++i;
    }
    if (!tabs.empty())
      active = std::min(active, tabs.size() - 1);
    if (auto text = clipboard.poll();
        text && !tabs.empty() && pasteTab == identities[&term()]) {
      term().pasteText(*text);
      dirty = true;
    }
    if (!tabs.empty())
      dirty = dirty || term().pending();
  }
};
static SDL_Scancode desktopScancode(int symbol) {
  if (symbol >= 'A' && symbol <= 'Z')
    symbol += 'a' - 'A';
  if (symbol >= 'a' && symbol <= 'z')
    return SDL_Scancode(SDL_SCANCODE_A + symbol - 'a');
  if (symbol >= '1' && symbol <= '9') return SDL_Scancode(SDL_SCANCODE_1+symbol-'1');
  if (symbol == '0') return SDL_SCANCODE_0;
  if (symbol >= FL_KP+'0' && symbol <= FL_KP+'9')
    return symbol==FL_KP+'0'?SDL_SCANCODE_KP_0:SDL_Scancode(SDL_SCANCODE_KP_1+symbol-FL_KP-'1');
  if (symbol > FL_F && symbol <= FL_F + 24)
    return SDL_Scancode(SDL_SCANCODE_F1 + symbol - FL_F - 1);
  switch (symbol) {
#define KEY(a, b)                                                              \
  case FL_##a:                                                                 \
    return SDL_SCANCODE_##b
    KEY(Enter, RETURN);
    KEY(KP_Enter, KP_ENTER);
    KEY(BackSpace, BACKSPACE);
    KEY(Tab, TAB);
    KEY(Escape, ESCAPE);
    KEY(Up, UP);
    KEY(Down, DOWN);
    KEY(Left, LEFT);
    KEY(Right, RIGHT);
    KEY(Home, HOME);
    KEY(End, END);
    KEY(Page_Up, PAGEUP);
    KEY(Page_Down, PAGEDOWN);
    KEY(Insert, INSERT);
    KEY(Delete, DELETE);
    KEY(Shift_L, LSHIFT); KEY(Shift_R, RSHIFT); KEY(Control_L, LCTRL); KEY(Control_R, RCTRL);
    KEY(Alt_L, LALT); KEY(Alt_R, RALT); KEY(Meta_L, LGUI); KEY(Meta_R, RGUI);
    KEY(Caps_Lock, CAPSLOCK); KEY(Num_Lock, NUMLOCKCLEAR); KEY(Scroll_Lock, SCROLLLOCK);
    KEY(Print, PRINTSCREEN); KEY(Pause, PAUSE); KEY(Menu, MENU);
#undef KEY
  case ' ': return SDL_SCANCODE_SPACE;
  case '-':return SDL_SCANCODE_MINUS;case '=':return SDL_SCANCODE_EQUALS;
  case '[':return SDL_SCANCODE_LEFTBRACKET;case ']':return SDL_SCANCODE_RIGHTBRACKET;
  case '\\':return SDL_SCANCODE_BACKSLASH;case ';':return SDL_SCANCODE_SEMICOLON;
  case '\'':return SDL_SCANCODE_APOSTROPHE;case '`':return SDL_SCANCODE_GRAVE;
  case ',':return SDL_SCANCODE_COMMA;case '.':return SDL_SCANCODE_PERIOD;case '/':return SDL_SCANCODE_SLASH;
  default:
    return SDL_SCANCODE_UNKNOWN;
  }
}
class Canvas : public Fl_Gl_Window {
  App &app;
  GLuint texture = 0;

public:
  Canvas(int x, int y, int w, int h, App &a)
      : Fl_Gl_Window(x, y, w, h), app(a) {
    mode(FL_RGB | FL_ALPHA | FL_DOUBLE);
    end();
  }
  ~Canvas() {
    if (context() && texture) {
      make_current();
      glDeleteTextures(1, &texture);
    }
  }
  void draw() override {
    if (app.tabs.empty())
      return;
    auto &r = app.renderer;
    if (!context_valid())
      texture = 0;
    glViewport(0, 0, pixel_w(), pixel_h());
    auto bg = r.background();
    glClearColor(bg[0] / 255.f, bg[1] / 255.f, bg[2] / 255.f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!texture)
      glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, r.width, r.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, r.pixels.data());
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w(), h(), 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1);
    glVertex2i(0, 0);
    glTexCoord2f(1, 1);
    glVertex2i(r.width, 0);
    glTexCoord2f(1, 0);
    glVertex2i(r.width, r.height);
    glTexCoord2f(0, 0);
    glVertex2i(0, r.height);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
  }
  void resize(int x, int y, int w, int h) override {
    Fl_Gl_Window::resize(x, y, w, h);
    app.options.cols = std::clamp(w / app.renderer.cw, 10, 240);
    app.options.rows = std::clamp(h / app.renderer.ch - 1, 2, 100);
    for (auto &t : app.tabs)
      t->grid(app.options.cols, app.options.rows);
    app.dirty = true;
  }
  int handle(int event) override {
    if (app.tabs.empty())
      return 1;
    auto &r = app.renderer;
    if (event == FL_FOCUS || event == FL_ENTER)
      return 1;
    if (event == FL_UNFOCUS) {
      r.preedit.clear();
      app.dirty = true;
      return 1;
    }
    if (event == FL_PASTE) {
      app.term().pasteText(std::string(Fl::event_text(), Fl::event_length()));
      app.dirty = true;
      return 1;
    }
    if (event == FL_PUSH || event == FL_DRAG || event == FL_RELEASE) {
      if (event == FL_PUSH)
        take_focus();
      // event_button() is defined only for push/release, not FL_DRAG.
      if (event == FL_DRAG ? (Fl::event_state() & FL_BUTTON1)
                           : Fl::event_button() == FL_LEFT_MOUSE) {
        // A GL subwindow receives coordinates local to its own surface.
        app.term().selectCells(Fl::event_x() / r.cw,
                               Fl::event_y() / r.ch, event == FL_PUSH);
        app.dirty = true;
      } else if (event == FL_RELEASE && Fl::event_button() == FL_MIDDLE_MOUSE) {
        if (app.options.wayland) {
          app.pasteTab = app.identities[&app.term()];
          app.clipboard.paste(true);
        } else Fl::paste(*this, 0);
      }
      if (event == FL_RELEASE && Fl::event_button() == FL_LEFT_MOUSE) {
        auto text = app.term().selectionText();
        if (!text.empty()) {
          if (app.options.wayland) app.clipboard.copy(text,true);
          else Fl::copy(text.data(), text.size(), 0);
        }
      }
      return 1;
    }
    if (event == FL_MOUSEWHEEL) {
      app.term().scrollRows(Fl::event_dy() * 3);
      app.dirty = true;
      return 1;
    }
    if (event == FL_KEYDOWN || event == FL_KEYUP) {
      SDL_KeyboardEvent key{};
      key.type = event == FL_KEYDOWN ? SDL_KEYDOWN : SDL_KEYUP;
      key.keysym.scancode = desktopScancode(Fl::event_original_key());
      if (Fl::event_key() < 0xff00) key.keysym.sym = xkb_keysym_to_utf32(Fl::event_key());
      int state = Fl::event_state();
      key.keysym.mod = (state & FL_CTRL ? KMOD_CTRL : 0) |
                       (state & FL_SHIFT ? KMOD_SHIFT : 0) |
                       (state & FL_ALT ? KMOD_ALT : 0) |
                       (state & FL_META ? KMOD_GUI : 0) |
                       (state & FL_CAPS_LOCK ? KMOD_CAPS : 0) |
                       (state & FL_NUM_LOCK ? KMOD_NUM : 0);
      if (event == FL_KEYDOWN && (state & FL_CTRL) && (state & FL_SHIFT) &&
          Fl::event_key() == 'v') {
        Fl::paste(*this, 1);
        return 1;
      }
      if (event == FL_KEYDOWN && !(state & (FL_CTRL | FL_ALT | FL_META))) {
        int del = 0;
        if (Fl::compose(del)) {
          if (Fl::compose_state) {
            r.preedit = std::string(Fl::event_text(), Fl::event_length());
            app.dirty = true;
            return 1;
          }
          r.preedit.clear();
        }
      }
      auto text=std::string(Fl::event_text(), Fl::event_length());
      if (app.desktopKeyboard && app.desktopKeyboard->ready())
        app.desktopKeys.push_back({unsigned(Fl::event_original_key()),key,std::move(text)});
      else app.key(key,text);
      return 1;
    }
    return Fl_Gl_Window::handle(event);
  }
};
class TerminalWindow : public Fl_Double_Window {
  App &app;
  Fl_Menu_Bar menu;
  Fl_Choice tabs;
  Canvas canvas;
  std::unique_ptr<DesktopMenu> exported;
  std::string signature;
  int cellWidth = 0, cellHeight = 0;
  static int controlsHeight() { return dx::dome() ? 34 : 64; }
  static void action(Fl_Widget *w, void *data) {
    auto self = static_cast<TerminalWindow *>(data);
    auto bar = static_cast<Fl_Menu_Bar *>(w);
    auto label = bar->mvalue()->label();
    auto &app = self->app;
    if (!strcmp(label, "New tab"))
      app.add();
    else if (!strcmp(label, "New window"))
      dx::spawn({std::filesystem::read_symlink("/proc/self/exe").string(),
                 "--wayland"});
    else if (!strcmp(label, "Close tab"))
      app.close();
    else if (!strcmp(label, "Quit"))
      stopped = 1;
    else if (!strcmp(label, "Copy")) {
      auto text = app.term().selectionText();
      Fl::copy(text.data(), text.size(), 1);
    } else if (!strcmp(label, "Paste"))
      Fl::paste(self->canvas, 1);
    else if (!strcmp(label, "Select all")) {
      app.term().selectAll();
      app.dirty = true;
    } else if (!strcmp(label, "Show XR terminal"))
      dx::spawn({std::filesystem::read_symlink("/proc/self/exe")
                         .parent_path()
                         .string() +
                     "/flterm-xr-launch",
                 "--placement", "20"});
    self->canvas.take_focus();
  }

public:
  explicit TerminalWindow(App &a)
      : Fl_Double_Window(a.renderer.width,
                         a.renderer.height + controlsHeight(), "FLTerm"),
        app(a), menu(0, 0, w(), 30),
        tabs(4, controlsHeight() - 32, w() - 8, 28),
        canvas(0, controlsHeight(), w(), h() - controlsHeight(), a) {
    xclass("org.domed.FLTerm");
    menu.add("File/New tab", FL_CTRL | FL_SHIFT | 't', action, this);
    menu.add("File/New window", FL_CTRL | FL_SHIFT | 'n', action, this);
    menu.add("File/Close tab", FL_CTRL | FL_SHIFT | 'w', action, this);
    menu.add("File/Quit", FL_CTRL | FL_SHIFT | 'q', action, this);
    menu.add("Edit/Copy", FL_CTRL | FL_SHIFT | 'c', action, this);
    menu.add("Edit/Paste", FL_CTRL | FL_SHIFT | 'v', action, this);
    menu.add("Edit/Select all", FL_CTRL | FL_SHIFT | 'a', action, this);
    menu.add("View/Show XR terminal", 0, action, this);
    tabs.callback(
        [](Fl_Widget *w, void *d) {
          auto self = static_cast<TerminalWindow *>(d);
          self->app.select(static_cast<Fl_Choice *>(w)->value());
          self->canvas.take_focus();
        },
        this);
    end();
    // Keep the command model for DBusMenu while DomeD renders the only menu bar.
    if (dx::dome())
      menu.hide();
    resizable(canvas);
    size_range(160, 160);
    callback([](Fl_Widget *, void *) { stopped = 1; });
    dx::style(this);
    show();
    canvas.take_focus();
    exported = std::make_unique<DesktopMenu>(*this, menu);
    app.menuService = exported->serviceName();
    app.menuPath = exported->objectPath();
  }
  void poll() {
    if (cellWidth != app.renderer.cw || cellHeight != app.renderer.ch) {
      cellWidth = app.renderer.cw;
      cellHeight = app.renderer.ch;
      canvas.resize(canvas.x(), canvas.y(), canvas.w(), canvas.h());
      app.render();
    }
    std::string next =
        std::to_string(app.tabs.size()) + ":" + std::to_string(app.active);
    if (next != signature) {
      tabs.clear();
      for (size_t i = 0; i < app.tabs.size(); ++i) {
        auto title = "Tab " + std::to_string(i + 1);
        tabs.add(title.c_str());
      }
      tabs.value(app.active);
      signature = next;
    }
    if (!app.tabs.empty()) {
      auto title = std::string(app.term().title());
      copy_label(title.empty() ? "FLTerm" : title.c_str());
    }
    if (shown() != app.visible) {
      if (app.visible) {
        show();
        canvas.take_focus();
      } else
        hide();
    }
    canvas.redraw();
  }
};
static void screenshot(const std::string &path, const Renderer &r) {
  std::ofstream out(path, std::ios::binary);
  if (!out)
    throw std::runtime_error("Cannot write screenshot");
  out << "P6\n" << r.width << ' ' << r.height << "\n255\n";
  for (int y = r.height - 1; y >= 0; --y)
    for (int x = 0; x < r.width; ++x)
      out.write(reinterpret_cast<const char *>(r.pixels.data() +
                                               (size_t(y) * r.width + x) * 4),
                3);
}
int main(int argc, char **argv) try {
  setlocale(LC_ALL, "");
  auto options = parse(argc, argv);
  sigset_t mask;
  sigemptyset(&mask);
  sigprocmask(SIG_SETMASK, &mask, nullptr);
  for (int sig : {SIGINT, SIGTERM, SIGHUP, SIGUSR1})
    signal(sig, signalHandler);
  signal(SIGPIPE, SIG_IGN);
  if (options.wayland) {
    if (!getenv("WAYLAND_DISPLAY"))
      throw std::runtime_error("--wayland requires WAYLAND_DISPLAY");
    setenv("FLTK_BACKEND", "wayland", 1);
  }
  Control control(options.desktop);
  App app(options);
  std::cerr << "Control: " << control.path << '\n';
  if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS))
    throw std::runtime_error(SDL_GetError());
  std::unique_ptr<TerminalWindow> window;
  if (options.desktop) {
    dx::init();
#ifdef FLTK_USE_WAYLAND
    if (options.wayland && !fl_wl_display())
      throw std::runtime_error(
          "FLTerm requires a native Wayland connection in --wayland mode");
#else
    if (options.wayland)
      throw std::runtime_error("This FLTK build has no Wayland backend");
#endif
    window = std::make_unique<TerminalWindow>(app);
#ifdef FLTK_USE_WAYLAND
    if (fl_wl_display()) app.desktopKeyboard=std::make_unique<DesktopKeyboard>(fl_wl_display());
#endif
  } else if (!options.headless) {
    app.graphics = std::make_unique<NativeGraphics>();
    app.xr = std::make_unique<XrOverlay>(
        *app.graphics, app.renderer.width, app.renderer.height,
        app.layout.width, -app.layout.z, app.layout.y, options.standalone,
        options.placement, options.blend.c_str());
    if (!options.noKeyboard) {
      app.keyboard = std::make_unique<NativeKeyboard>();
      app.pointer = std::make_unique<NativePointer>(
          app.renderer.width, options.rows * app.renderer.ch);
    }
  }
  auto start = std::chrono::steady_clock::now(), nextAppearance = start;
  auto saved = app.layout;
  while (!stopped && !app.tabs.empty() && (!app.xr || !app.xr->quit)) {
    auto now = std::chrono::steady_clock::now();
    if (options.seconds > 0 &&
        std::chrono::duration<float>(now - start).count() >= options.seconds)
      break;
    if (toggleRequested) {
      app.visible = !app.visible;
      toggleRequested = 0;
    }
    if (app.xr)
      app.xr->events();
    if (now >= nextAppearance) {
      try {
        app.appearance();
      } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
      }
      nextAppearance = now + std::chrono::milliseconds(300);
    }
    app.control(control);
    app.pump();
    if (app.tabs.empty())
      break;
    if (app.keyboard) {
      for (auto &input : app.keyboard->pump())
        if (app.visible) app.key(input.event,input.identity.text,&input.identity);
      app.keyboard->focus(app.visible && app.xr->running && !app.xr->quit);
    }
    if (app.pointer) {
      for (auto &event : app.pointer->pump()) {
        if (event.type == SDL_MOUSEWHEEL) {
          app.term().scrollRows(-event.wheel.y * 3);
          app.dirty = true;
        } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                   event.button.button == SDL_BUTTON_LEFT) {
          app.selecting = true;
          app.term().selectCells(event.button.x / app.renderer.cw,
                                 event.button.y / app.renderer.ch, true);
          app.dirty = true;
        } else if (event.type == SDL_MOUSEMOTION && app.selecting) {
          app.term().selectCells(event.motion.x / app.renderer.cw,
                                 event.motion.y / app.renderer.ch, false);
          app.dirty = true;
        } else if (event.type == SDL_MOUSEBUTTONUP &&
                   event.button.button == SDL_BUTTON_LEFT) {
          app.selecting = false;
          app.clipboard.copy(app.term().selectionText(), true);
        } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                   event.button.button == SDL_BUTTON_MIDDLE) {
          app.pasteTab = app.identities[&app.term()];
          app.clipboard.paste(true);
        }
      }
      app.pointer->focus(app.visible && app.xr->running && !app.xr->quit);
    }
    if (app.dirty)
      app.render();
    if (app.xr) {
      app.xr->meters = app.layout.width;
      app.xr->panelHeight = app.layout.height;
      app.xr->horizontal = app.layout.x;
      app.xr->vertical = app.layout.y;
      app.xr->distance = -app.layout.z;
      app.xr->frame(app.visible, app.renderer.pixels);
    }
    if (options.persist) {
      if (!(saved == app.layout)) {
        app.state.changed(app.layout);
        saved = app.layout;
      }
      app.state.tick();
    }
    if (window) {
      window->poll();
      Fl::wait(.01);
      // Both Wayland listeners have now dispatched this batch. Resolve FLTK's
      // committed text against the compositor's physical key and actual layout.
      for (auto &pending : app.desktopKeys) {
        auto input=app.desktopKeyboard->resolve(pending.symbol,pending.event,pending.text);
        app.key(input.event,input.identity.text,&input.identity);
      }
      app.desktopKeys.clear();
    } else if (!app.xr || !app.xr->running)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (app.keyboard)
    app.keyboard->focus(false);
  if (app.pointer)
    app.pointer->focus(false);
  if (options.persist)
    app.state.flush();
  if (!options.screenshot.empty())
    screenshot(options.screenshot, app.renderer);
  if (!options.report.empty()) {
    std::ofstream report(options.report);
    report << app.status().dump(2) << '\n';
  }
  window.reset();
  return 0;
} catch (const std::exception &error) {
  std::cerr << "flterm-xr: " << error.what() << '\n';
  return 1;
}
