// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "kitty-keyboard.hpp"
#include <SDL.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

struct KeyInput {
  SDL_KeyboardEvent event{};
  kitty_keyboard::Key identity;
  unsigned logical = 0, physical = 0;
};
inline unsigned baseKey(SDL_Scancode sc) {
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
    return 'a' + sc - SDL_SCANCODE_A;
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
    return '1' + sc - SDL_SCANCODE_1;
  switch (sc) {
  case SDL_SCANCODE_0:
    return '0';
  case SDL_SCANCODE_SPACE:
    return ' ';
  case SDL_SCANCODE_MINUS:
    return '-';
  case SDL_SCANCODE_EQUALS:
    return '=';
  case SDL_SCANCODE_LEFTBRACKET:
    return '[';
  case SDL_SCANCODE_RIGHTBRACKET:
    return ']';
  case SDL_SCANCODE_BACKSLASH:
    return '\\';
  case SDL_SCANCODE_SEMICOLON:
    return ';';
  case SDL_SCANCODE_APOSTROPHE:
    return '\'';
  case SDL_SCANCODE_GRAVE:
    return '`';
  case SDL_SCANCODE_COMMA:
    return ',';
  case SDL_SCANCODE_PERIOD:
    return '.';
  case SDL_SCANCODE_SLASH:
    return '/';
  default:
    return 0;
  }
}
inline kitty_keyboard::Key inputKey(const SDL_KeyboardEvent &event,
                                    std::string text = {}) {
  using namespace kitty_keyboard;
  Key key;
  key.text = std::move(text);
  key.code = baseKey(event.keysym.scancode);
  key.base = key.code;
  key.event = event.type == SDL_KEYUP ? 3 : event.repeat ? 2 : 1;
  unsigned mod = event.keysym.mod;
  key.modifiers = (mod & KMOD_SHIFT ? Shift : 0) | (mod & KMOD_ALT ? Alt : 0) |
                  (mod & KMOD_CTRL ? kitty_keyboard::Control : 0) |
                  (mod & KMOD_GUI ? Super : 0) |
                  (mod & KMOD_CAPS ? CapsLock : 0) |
                  (mod & KMOD_NUM ? NumLock : 0);
  key.functional = !key.code;
  auto sc = event.keysym.scancode;
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) {
    static constexpr unsigned codes[] = {1,  1,  13, 1,  15, 17,
                                         18, 19, 20, 21, 23, 24};
    static constexpr char finals[] = {'P', 'Q', '~', 'S', '~', '~',
                                      '~', '~', '~', '~', '~', '~'};
    key.code = codes[sc - SDL_SCANCODE_F1];
    key.final = finals[sc - SDL_SCANCODE_F1];
  } else if (sc >= SDL_SCANCODE_F13 && sc <= SDL_SCANCODE_F24)
    key.code = 57376 + sc - SDL_SCANCODE_F13;
  else if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9)
    key.code = 57400 + sc - SDL_SCANCODE_KP_1;
  else
    switch (sc) {
#define KEY(sc, code_, final_)                                                 \
  case SDL_SCANCODE_##sc:                                                      \
    key.code = code_;                                                          \
    key.final = final_;                                                        \
    break
      KEY(ESCAPE, 27, 'u');
      KEY(RETURN, 13, 'u');
      KEY(TAB, 9, 'u');
      KEY(BACKSPACE, 127, 'u');
      KEY(INSERT, 2, '~');
      KEY(DELETE, 3, '~');
      KEY(PAGEUP, 5, '~');
      KEY(PAGEDOWN, 6, '~');
      KEY(UP, 1, 'A');
      KEY(DOWN, 1, 'B');
      KEY(RIGHT, 1, 'C');
      KEY(LEFT, 1, 'D');
      KEY(HOME, 1, 'H');
      KEY(END, 1, 'F');
      KEY(CAPSLOCK, 57358, 'u');
      KEY(SCROLLLOCK, 57359, 'u');
      KEY(NUMLOCKCLEAR, 57360, 'u');
      KEY(PRINTSCREEN, 57361, 'u');
      KEY(PAUSE, 57362, 'u');
      KEY(APPLICATION, 57363, 'u');
      KEY(MENU, 57363, 'u');
      KEY(KP_0, 57399, 'u');
      KEY(KP_PERIOD, 57409, 'u');
      KEY(KP_DIVIDE, 57410, 'u');
      KEY(KP_MULTIPLY, 57411, 'u');
      KEY(KP_MINUS, 57412, 'u');
      KEY(KP_PLUS, 57413, 'u');
      KEY(KP_ENTER, 57414, 'u');
      KEY(KP_EQUALS, 57415, 'u');
      KEY(KP_COMMA, 57416, 'u');
      KEY(AUDIOPLAY, 57430, 'u');
      KEY(AUDIOSTOP, 57432, 'u');
      KEY(AUDIONEXT, 57435, 'u');
      KEY(AUDIOPREV, 57436, 'u');
      KEY(VOLUMEDOWN, 57438, 'u');
      KEY(VOLUMEUP, 57439, 'u');
      KEY(AUDIOMUTE, 57440, 'u');
      KEY(AUDIOREWIND, 57434, 'u');
      KEY(AUDIOFASTFORWARD, 57433, 'u');
      KEY(LCTRL, 57442, 'u');
      KEY(LSHIFT, 57441, 'u');
      KEY(LALT, 57443, 'u');
      KEY(LGUI, 57444, 'u');
      KEY(RCTRL, 57448, 'u');
      KEY(RSHIFT, 57447, 'u');
      KEY(RALT, 57449, 'u');
      KEY(RGUI, 57450, 'u');
#undef KEY
    default:
      break;
    }
  key.modifier = (sc >= SDL_SCANCODE_LCTRL && sc <= SDL_SCANCODE_RGUI) ||
                 sc == SDL_SCANCODE_CAPSLOCK || sc == SDL_SCANCODE_NUMLOCKCLEAR;
  if (!key.functional) {
    if (key.code >= 'a' && key.code <= 'z')
      key.shifted = key.code - 'a' + 'A';
    else {
      std::string_view plain = "1234567890-=[]\\;'`,./",
                       shifted = "!@#$%^&*()_+{}|:\"~<>?";
      auto at = plain.find(char(key.code));
      if (at != plain.npos)
        key.shifted = shifted[at];
    }
  }
  if (event.keysym.sym && !(event.keysym.sym & SDLK_SCANCODE_MASK)) {
    key.code = event.keysym.sym;
    key.functional = false;
  }
  if (!key.code && !key.text.empty())
    key.functional = false;
  return key;
}
