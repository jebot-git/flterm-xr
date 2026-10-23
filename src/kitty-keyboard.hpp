// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace kitty_keyboard {
enum Modifier : unsigned {
  Shift = 1,
  Alt = 2,
  Control = 4,
  Super = 8,
  Hyper = 16,
  Meta = 32,
  CapsLock = 64,
  NumLock = 128
};
struct Key {
  unsigned code = 0, shifted = 0, base = 0, modifiers = 0, event = 1;
  char final = 'u';
  bool functional = false, modifier = false, composing = false;
  std::string text;
};
inline std::vector<unsigned> codepoints(std::string_view text) {
  std::vector<unsigned> out;
  for (size_t i = 0; i < text.size();) {
    unsigned cp = static_cast<unsigned char>(text[i++]);
    int n = cp < 128              ? 0
            : (cp & 0xe0) == 0xc0 ? 1
            : (cp & 0xf0) == 0xe0 ? 2
            : (cp & 0xf8) == 0xf0 ? 3
                                  : 0;
    cp &= n == 1 ? 31 : n == 2 ? 15 : n == 3 ? 7 : 255;
    while (n-- && i < text.size())
      cp = (cp << 6) | (static_cast<unsigned char>(text[i++]) & 63);
    out.push_back(cp);
  }
  return out;
}
inline std::string utf8(unsigned cp) {
  std::string out;
  if (cp < 128)
    out += char(cp);
  else if (cp < 2048) {
    out += char(0xc0 | (cp >> 6));
    out += char(0x80 | (cp & 63));
  } else if (cp < 65536) {
    out += char(0xe0 | (cp >> 12));
    out += char(0x80 | ((cp >> 6) & 63));
    out += char(0x80 | (cp & 63));
  } else {
    out += char(0xf0 | (cp >> 18));
    out += char(0x80 | ((cp >> 12) & 63));
    out += char(0x80 | ((cp >> 6) & 63));
    out += char(0x80 | (cp & 63));
  }
  return out;
}
inline std::string encode(Key key, unsigned flags,
                          bool applicationCursor = false) {
  const bool all = flags & 8, events = flags & 2,
             disambiguate = (flags & 1) || all;
  if (key.composing || (key.modifier && !all) || (key.event == 3 && !events))
    return {};
  unsigned mods = key.modifiers, shortcuts = mods & 63;
  const bool keypad =
      key.code >= 57399 && key.code <= 57427 && key.final == 'u';
  if (keypad && !all && !key.text.empty() && !(shortcuts & ~Shift) &&
      key.event != 3)
    return key.text;
  if (keypad && !disambiguate) {
    if (key.code <= 57408) {
      key.code = '0' + key.code - 57399;
      key.functional = false;
    } else {
      switch (key.code) {
      case 57409:
        key.code = '.';
        key.functional = false;
        break;
      case 57410:
        key.code = '/';
        key.functional = false;
        break;
      case 57411:
        key.code = '*';
        key.functional = false;
        break;
      case 57412:
        key.code = '-';
        key.functional = false;
        break;
      case 57413:
        key.code = '+';
        key.functional = false;
        break;
      case 57414:
        key.code = 13;
        break;
      case 57415:
        key.code = '=';
        key.functional = false;
        break;
      case 57416:
        key.code = ',';
        key.functional = false;
        break;
      case 57417:
        key.code = 1;
        key.final = 'D';
        break;
      case 57418:
        key.code = 1;
        key.final = 'C';
        break;
      case 57419:
        key.code = 1;
        key.final = 'A';
        break;
      case 57420:
        key.code = 1;
        key.final = 'B';
        break;
      case 57421:
        key.code = 5;
        key.final = '~';
        break;
      case 57422:
        key.code = 6;
        key.final = '~';
        break;
      case 57423:
        key.code = 1;
        key.final = 'H';
        break;
      case 57424:
        key.code = 1;
        key.final = 'F';
        break;
      case 57425:
        key.code = 2;
        key.final = '~';
        break;
      case 57426:
        key.code = 3;
        key.final = '~';
        break;
      case 57427:
        key.code = 1;
        key.final = 'E';
        break;
      }
    }
  }
  const bool c0 = key.functional && key.final == 'u' &&
                  (key.code == 13 || key.code == 9 || key.code == 127);
  // Unmodified Enter/Tab/Backspace retain recovery-shell encodings. Modified
  // presses (notably Codex's Shift+Enter) use CSI u with disambiguation
  // enabled.
  if (c0 && !all) {
    if (key.event == 3)
      return {};
    if (!disambiguate || !shortcuts) {
      std::string result =
          key.code == 9 && (mods & Shift)
              ? "\033[Z"
              : std::string(
                    1,
                    char(key.code == 127 && (mods & Control) ? 8 : key.code));
      return (mods & Alt ? "\033" : "") + result;
    }
  }
  bool escaped = all || key.functional ||
                 (disambiguate && (shortcuts & ~Shift)) ||
                 (shortcuts & (Super | Hyper | Meta)) ||
                 ((shortcuts & (Shift | Control)) == (Shift | Control)) ||
                 (!key.functional && key.code > 127 && (shortcuts & ~Shift)) ||
                 (events && key.event == 3);
  if (!escaped) {
    if (key.event == 3)
      return {};
    std::string result;
    if (mods & Control) {
      unsigned cp = key.code;
      if (cp >= 'a' && cp <= 'z')
        result += char(cp - 'a' + 1);
      else if (cp == ' ' || cp == '2' || cp == '@')
        result += '\0';
      else if (cp == '[' || cp == '3')
        result += char(27);
      else if (cp == '\\' || cp == '4')
        result += char(28);
      else if (cp == ']' || cp == '5')
        result += char(29);
      else if (cp == '^' || cp == '6')
        result += char(30);
      else if (cp == '_' || cp == '/' || cp == '7')
        result += char(31);
      else if (cp == '?' || cp == '8')
        result += char(127);
      else
        result = utf8(cp);
    } else
      result =
          !key.text.empty()
              ? key.text
              : utf8((mods & Shift) && key.shifted ? key.shifted : key.code);
    if (!key.code && key.text.empty())
      return {};
    return (mods & Alt ? "\033" : "") + result;
  }
  // Escape itself remains a legacy byte until disambiguation is enabled.
  if (key.code == 27 && key.final == 'u' && !disambiguate) {
    if (key.event == 3)
      return {};
    return mods & Alt ? "\033\033" : "\033";
  }
  if (!key.code && key.text.empty())
    return {};
  if (keypad && disambiguate && key.code == 57427) {
    key.code = 1;
    key.final = 'E';
  }
  unsigned encodedMods =
      1 + ((all || key.functional) && flags ? mods : shortcuts);
  // Legacy cursor/function sequences stay compatible with xterm terminfo.
  if (!flags && key.final != 'u' && encodedMods == 1) {
    if (key.final == '~' && key.code == 13)
      return "\033OR";
    bool ss3 = (key.final >= 'P' && key.final <= 'S') ||
               (applicationCursor && std::string_view("ABCDHF").find(
                                         key.final) != std::string_view::npos);
    if (ss3)
      return std::string("\033O") + key.final;
  }
  std::string result = "\033[";
  bool alternate = (flags & 4) && key.final == 'u' && !key.functional;
  bool text = (flags & 16) && key.event != 3 && !key.text.empty() &&
              !(shortcuts & ~Shift);
  bool event = events && key.event != 1;
  bool modifiers = encodedMods != 1 || event || text;
  if (key.final == 'u' || key.final == '~' || modifiers)
    result += std::to_string(key.code);
  if (alternate) {
    bool shifted = (mods & Shift) && key.shifted && key.shifted != key.code;
    bool base = key.base && key.base != key.code;
    if (shifted || base) {
      result += ':';
      if (shifted)
        result += std::to_string(key.shifted);
    }
    if (base)
      result += ':' + std::to_string(key.base);
  }
  if (modifiers) {
    result += ';' + std::to_string(encodedMods);
    if (event)
      result += ':' + std::to_string(key.event);
  }
  if (text) {
    std::string content;
    for (auto cp : codepoints(key.text))
      if (cp >= 32 && !(cp >= 127 && cp <= 159)) {
        if (!content.empty())
          content += ':';
        content += std::to_string(cp);
      }
    if (!content.empty())
      result += ';' + content;
  }
  return result + key.final;
}
// https://sw.kovidgoyal.net/kitty/keyboard-protocol/#progressive-enhancement
class Modes {
  std::array<std::vector<unsigned>, 2> stacks_;

public:
  unsigned flags(bool alternate) const {
    const auto &s = stacks_[alternate];
    return s.empty() ? 0 : s.back();
  }
  bool command(std::string_view sequence, bool alternate, std::string &reply) {
    if (sequence.size() < 2 || sequence.back() != 'u' ||
        std::string_view("?<>=").find(sequence.front()) ==
            std::string_view::npos)
      return false;
    const char op = sequence.front();
    auto args = sequence.substr(1, sequence.size() - 2);
    unsigned value = op == '<' ? 1 : 0, mode = 1;
    auto split = args.find(';');
    auto parse = [](std::string_view part, unsigned &result) {
      if (part.empty())
        return true;
      auto [end, error] =
          std::from_chars(part.data(), part.data() + part.size(), result);
      return error == std::errc{} && end == part.data() + part.size();
    };
    if (!parse(args.substr(0, split), value) ||
        (split != args.npos && !parse(args.substr(split + 1), mode)))
      return true;
    auto &stack = stacks_[alternate];
    if (op == '?')
      reply = "\033[?" + std::to_string(flags(alternate)) + "u";
    else if (op == '>') {
      if (stack.size() == 32)
        stack.erase(stack.begin());
      stack.push_back(value & 31);
    } else if (op == '<') {
      stack.resize(stack.size() - std::min<size_t>(value, stack.size()));
    } else if (mode >= 1 && mode <= 3) {
      unsigned next = mode == 1   ? value
                      : mode == 2 ? flags(alternate) | value
                                  : flags(alternate) & ~value;
      if (stack.empty())
        stack.push_back(0);
      stack.back() = next & 31;
    }
    return true;
  }
  void reset() { stacks_ = {}; }
};
} // namespace kitty_keyboard
