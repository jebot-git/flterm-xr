// SPDX-License-Identifier: GPL-3.0-or-later
#include "key-input.hpp"
#include "keyboard.hpp"
#include <iostream>
#include <linux/input-event-codes.h>
#include <stdexcept>
#include <tuple>
using namespace kitty_keyboard;
static void expect(const Key &key, unsigned flags,
                   const std::string &expected) {
  auto actual = encode(key, flags);
  if (actual != expected)
    throw std::runtime_error("Key " + std::to_string(key.code) + " flags " +
                             std::to_string(flags) + ": expected " + expected +
                             " got " + actual);
}
static Key letter(unsigned mods = 0, unsigned event = 1,
                  std::string text = "a") {
  Key key;
  key.code = 'a';
  key.shifted = 'A';
  key.base = 'a';
  key.modifiers = mods;
  key.event = event;
  key.text = text;
  return key;
}
int main() {
  expect(letter(), 0, "a");
  expect(letter(Control, 1, ""), 0, std::string(1, 1));
  expect(letter(Alt), 0, "\033a");
  expect(letter(Shift | Control, 1, ""), 1, "\033[97;6u");
  expect(letter(Control, 1, ""), 1, "\033[97;5u");
  expect(letter(Alt), 1, "\033[97;3u");
  expect(letter(Shift, 1, "A"), 1, "A");
  expect(letter(0, 2), 7, "a");
  expect(letter(0, 3, ""), 7, "\033[97;1:3u");
  expect(letter(0, 2), 8, "\033[97u");
  expect(letter(0, 3, ""), 8, "");
  expect(letter(0, 2), 10, "\033[97;1:2u");
  expect(letter(0, 3, ""), 10, "\033[97;1:3u");
  expect(letter(Shift | Control, 2, ""), 7, "\033[97:65;6:2u");
  expect(letter(Shift, 1, "A"), 31, "\033[97:65;2;65u");
  auto cyrillic = letter(Control, 1, "");
  cyrillic.code = 1089;
  cyrillic.shifted = 1057;
  cyrillic.base = 99;
  expect(cyrillic, 5, "\033[1089::99;5u");
  Key ime;
  ime.text = "日本";
  expect(ime, 24, "\033[0;1;26085:26412u");
  expect(ime, 0, "日本");
  Key enter;
  enter.code = 13;
  enter.functional = true;
  expect(enter, 7, "\r");
  enter.modifiers = Alt | Shift;
  expect(enter, 7, "\033[13;4u");
  enter.modifiers = Shift;
  expect(enter, 1, "\033[13;2u");
  Key tab = enter;
  tab.code = 9;
  expect(tab, 1, "\033[9;2u");
  enter.modifiers = Alt | Shift;
  enter.event = 3;
  expect(enter, 7, "");
  expect(enter, 31, "\033[13;4:3u");
  Key escape;
  escape.code = 27;
  escape.functional = true;
  expect(escape, 0, "\033");
  expect(escape, 1, "\033[27u");
  Key arrow;
  arrow.code = 1;
  arrow.final = 'A';
  arrow.functional = true;
  expect(arrow, 1, "\033[A");
  arrow.event = 3;
  expect(arrow, 3, "\033[1;1:3A");
  Key modifier;
  modifier.code = 57441;
  modifier.modifier = true;
  modifier.functional = true;
  modifier.modifiers = Shift;
  expect(modifier, 7, "");
  expect(modifier, 8, "\033[57441;2u");
  modifier.modifiers = 0;
  modifier.event = 3;
  expect(modifier, 31, "\033[57441;1:3u");
  Key keypad;
  keypad.code = 57400;
  keypad.functional = true;
  keypad.text = "1";
  expect(keypad, 0, "1");
  expect(keypad, 1, "1");
  expect(keypad, 24, "\033[57400;1;49u");
  for (unsigned bit = 0; bit < 8; ++bit)
    expect(letter(1u << bit), 8,
           "\033[97;" + std::to_string(1 + (1u << bit)) + "u");
  // All mapped functional keys must encode press, repeat and release.
  for (int sc = SDL_SCANCODE_F1; sc <= SDL_SCANCODE_F24; ++sc) {
    if (sc > SDL_SCANCODE_F12 && sc < SDL_SCANCODE_F13)
      continue;
    SDL_KeyboardEvent event{};
    event.type = SDL_KEYDOWN;
    event.keysym.scancode = SDL_Scancode(sc);
    auto key = inputKey(event);
    if (encode(key, 31).empty())
      throw std::runtime_error("Missing functional key");
    key.event = 3;
    if (encode(key, 31).find(":3") == std::string::npos)
      throw std::runtime_error("Missing release");
  }
  auto context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  xkb_rule_names names{};
  names.layout = "us,cz";
  names.variant = nullptr;
  auto keymap =
      xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  {
    KeyboardTranslator keyboard(keymap, nullptr, 1);
    auto key = keyboard.translate(KEY_Y, 1);
    expect(key.identity, 31, "\033[122::121;1;122u");
    key = keyboard.translate(KEY_Y, 2);
    expect(key.identity, 31, "\033[122::121;1:2;122u");
    key = keyboard.translate(KEY_Y, 0);
    expect(key.identity, 31, "\033[122::121;1:3u");
    keyboard.translate(KEY_LEFTSHIFT, 1);
    key = keyboard.translate(KEY_Y, 1);
    expect(key.identity, 31, "\033[122:90:121;2;90u");
    keyboard.translate(KEY_Y, 0);
    keyboard.translate(KEY_LEFTSHIFT, 0);
    keyboard.translate(KEY_RIGHTALT, 1);
    key = keyboard.translate(KEY_E, 1);
    expect(key.identity, 31, "\033[8364::101;1;8364u");
    keyboard.translate(KEY_E, 0);
    keyboard.translate(KEY_RIGHTALT, 0);
    keyboard.translate(KEY_LEFTALT, 1);
    key = keyboard.translate(KEY_A, 1);
    expect(key.identity, 31, "\033[97;3u");
    expect(key.identity, 0, "\033a");
  }
  xkb_keymap_unref(keymap);
  for (auto [options, code, modifier] :
       {std::tuple{"altwin:meta_win", 57446u, 32u},
        std::tuple{"altwin:hyper_win", 57445u, 16u}}) {
    names.layout = "us";
    names.options = options;
    keymap =
        xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    KeyboardTranslator keyboard(keymap, nullptr);
    auto key = keyboard.translate(KEY_LEFTMETA, 1);
    expect(key.identity, 31,
           "\033[" + std::to_string(code) + ";" + std::to_string(modifier + 1) +
               "u");
    key = keyboard.translate(KEY_LEFTMETA, 0);
    expect(key.identity, 31, "\033[" + std::to_string(code) + ";1:3u");
    xkb_keymap_unref(keymap);
  }
  xkb_context_unref(context);
  Modes modes;
  std::string reply;
  for (int i = 0; i < 1000; ++i)
    modes.command(">31u", false, reply);
  modes.command("<999999u", false, reply);
  if (modes.flags(false))
    throw std::runtime_error("Stack underflow");
  std::cout
      << "PASS: all five Kitty enhancements, legacy fallback, Unicode/layout "
         "identity, locks, keypad, events, IME and bounded stacks\n";
}
