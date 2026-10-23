// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop-keyboard.hpp"
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
struct DesktopKeyboard::Impl {
  wl_registry *registry{};
  wl_seat *seat{};
  wl_keyboard *keyboard{};
  xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  std::unique_ptr<KeyboardTranslator> translator;
  std::deque<KeyInput> events;
  std::map<unsigned, KeyInput> held;
  static void keymap(void *data, wl_keyboard *, uint32_t format, int fd,
                     uint32_t size) {
    auto &self = *static_cast<Impl *>(data);
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && size && self.context) {
      auto map = static_cast<char *>(
          mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
      if (map != MAP_FAILED) {
        auto keymap = xkb_keymap_new_from_string(self.context, map,
                                                 XKB_KEYMAP_FORMAT_TEXT_V1,
                                                 XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (keymap) {
          self.translator =
              std::make_unique<KeyboardTranslator>(keymap, nullptr);
          xkb_keymap_unref(keymap);
        }
        munmap(map, size);
      }
    }
    close(fd);
  }
  static void enter(void *data, wl_keyboard *, uint32_t, wl_surface *,
                    wl_array *keys) {
    auto &self = *static_cast<Impl *>(data);
    if (!self.translator)
      return;
    auto codes = static_cast<const uint32_t *>(keys->data);
    for (size_t i = 0; i < keys->size / sizeof(*codes); ++i)
      self.translator->translate(codes[i], 1);
  }
  static void leave(void *data, wl_keyboard *, uint32_t, wl_surface *) {
    auto &self = *static_cast<Impl *>(data);
    self.events.clear();
    self.held.clear();
    if (self.translator)
      self.translator->resetKeys();
  }
  static void key(void *data, wl_keyboard *, uint32_t, uint32_t, uint32_t code,
                  uint32_t state) {
    auto &self = *static_cast<Impl *>(data);
    if (!self.translator)
      return;
    self.events.push_back(self.translator->translate(code, state ? 1 : 0));
    if (self.events.size() > 128)
      self.events.pop_front();
  }
  static void modifiers(void *data, wl_keyboard *, uint32_t, uint32_t depressed,
                        uint32_t latched, uint32_t locked, uint32_t group) {
    auto &self = *static_cast<Impl *>(data);
    if (self.translator)
      self.translator->modifiers(depressed, latched, locked, group);
  }
  static void repeat(void *, wl_keyboard *, int32_t, int32_t) {}
  static void capabilities(void *data, wl_seat *seat, uint32_t caps) {
    auto &self = *static_cast<Impl *>(data);
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !self.keyboard) {
      self.keyboard = wl_seat_get_keyboard(seat);
      static const wl_keyboard_listener listener = {keymap, enter,     leave,
                                                    key,    modifiers, repeat};
      wl_keyboard_add_listener(self.keyboard, &listener, &self);
    }
  }
  static void name(void *, wl_seat *, const char *) {}
  static void global(void *data, wl_registry *registry, uint32_t name,
                     const char *interface, uint32_t version) {
    auto &self = *static_cast<Impl *>(data);
    if (!strcmp(interface, "wl_seat") && !self.seat) {
      self.seat = static_cast<wl_seat *>(wl_registry_bind(
          registry, name, &wl_seat_interface, std::min(version, 7u)));
      static const wl_seat_listener listener = {capabilities, Impl::name};
      wl_seat_add_listener(self.seat, &listener, &self);
    }
  }
  static void removed(void *, wl_registry *, uint32_t) {}
  explicit Impl(wl_display *display) {
    if (!display)
      return;
    registry = wl_display_get_registry(display);
    static const wl_registry_listener listener = {global, removed};
    wl_registry_add_listener(registry, &listener, this);
    // Registry -> seat capabilities -> keymap, without consuming user input.
    for (int i = 0; i < 3; ++i)
      if (wl_display_roundtrip(display) < 0)
        break;
  }
  ~Impl() {
    if (keyboard)
      wl_keyboard_release(keyboard);
    if (seat)
      wl_seat_release(seat);
    if (registry)
      wl_registry_destroy(registry);
    if (context)
      xkb_context_unref(context);
  }
};
DesktopKeyboard::DesktopKeyboard(wl_display *display)
    : impl_(std::make_unique<Impl>(display)) {}
DesktopKeyboard::~DesktopKeyboard() = default;
bool DesktopKeyboard::ready() const { return bool(impl_->translator); }
KeyInput DesktopKeyboard::resolve(unsigned symbol,
                                  const SDL_KeyboardEvent &event,
                                  const std::string &text) {
  auto &self = *impl_;
  auto found = std::find_if(
      self.events.begin(), self.events.end(), [&](const auto &input) {
        return input.logical == symbol && input.event.type == event.type;
      });
  KeyInput input;
  if (found != self.events.end()) {
    input = *found;
    self.events.erase(self.events.begin(), found + 1);
    if (event.type == SDL_KEYDOWN)
      self.held[symbol] = input;
    else
      std::erase_if(self.held, [&](const auto &entry) {
        return entry.second.physical == input.physical;
      });
  } else if (auto held = self.held.find(symbol); held != self.held.end()) {
    input = self.translator->translate(held->second.physical,
                                       event.type == SDL_KEYDOWN ? 2 : 0);
    input.event = event;
    input.event.repeat = event.type == SDL_KEYDOWN;
    input.identity.event = event.type == SDL_KEYUP ? 3 : 2;
    if (event.type == SDL_KEYUP)
      self.held.erase(held);
  } else {
    input.event = event;
    input.identity = inputKey(event, text);
    if (!text.empty() && event.type == SDL_KEYDOWN &&
        !(event.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) {
      // IME commits have text but no corresponding physical key event.
      input.identity.code = 0;
      input.identity.functional = false;
    }
  }
  if (event.type == SDL_KEYDOWN) {
    input.identity.text = text;
    auto points = kitty_keyboard::codepoints(text);
    if (std::any_of(points.begin(), points.end(), [](unsigned cp) {
          return cp < 32 || (cp >= 127 && cp <= 159);
        }))
      input.identity.text.clear();
  } else
    input.identity.text.clear();
  return input;
}
